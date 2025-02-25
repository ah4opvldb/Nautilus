#include <unordered_map>
#include <list>
#include <mutex>
#include <cstdint>

#define wtlfu
// #define lru

struct CacheItem {
    uint64_t key = 0;
    union {
        uint64_t from;
        uint64_t* pointer;
    };

    uint64_t cached_version = 0;
    uint64_t cached_idx = 0;
    uint64_t cached_pointer = 0; // 注意放入缓存时地址48-64位的转换
    uint64_t freq = 0; // 访问频率
};

// LRU 缓存实现
class LRUCache {
public:
    LRUCache(uint64_t capacity) : capacity(capacity) {}

    bool get(uint64_t key, CacheItem& item) {
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) return false;

        // 将最近访问的项移动到头部
        accessOrder.splice(accessOrder.begin(), accessOrder, it->second);
        item = it->second->second;
        // printf("get key from cache: %ld\n", item.key);
        return true;
    }

    bool lock_free_get(uint64_t key, CacheItem& item) {
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) return false;

        item = it->second->second;
        // printf("get key from cache: %ld\n", item.key);
        return true;
    }

    void put(const CacheItem& item) {
        auto it = cacheMap.find(item.key);

        if (it != cacheMap.end()) {
            // 更新存在的项并移动到头部
            it->second->second = item;
            accessOrder.splice(accessOrder.begin(), accessOrder, it->second);
        } else {
            // 达到容量限制时移除最旧项
            if (cacheMap.size() >= capacity) {
                cacheMap.erase(accessOrder.back().first);
                accessOrder.pop_back();
            }
            // 插入新项到头部
            accessOrder.emplace_front(item.key, item);
            cacheMap[item.key] = accessOrder.begin();
        }
    }

    bool evict(CacheItem& item) {
        if (cacheMap.empty()) return false;

        item = accessOrder.back().second; // 取出最旧项
        cacheMap.erase(accessOrder.back().first);
        accessOrder.pop_back();
        return true;
    }
    void remove(uint64_t key) {
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            accessOrder.erase(it->second);
            cacheMap.erase(it);
        }
    }
    bool get_last(CacheItem& item) {
        if (cacheMap.empty()) return false;

        item = accessOrder.back().second; // 取出最旧项
        return true;
    }

    uint64_t getSize() const { return cacheMap.size(); }
    uint64_t getCapacity() const { return capacity; }

    void setCapacity(uint64_t newCapacity) {
        capacity = newCapacity;
        while (cacheMap.size() > capacity) {
            cacheMap.erase(accessOrder.back().first);
            accessOrder.pop_back();
        }
    }

private:
    uint64_t capacity;
    std::list<std::pair<uint64_t, CacheItem>> accessOrder;  // 用于维护 LRU 顺序
    std::unordered_map<uint64_t, std::list<std::pair<uint64_t, CacheItem>>::iterator> cacheMap; // 用于快速查找
};


// 实现为FIFO
class FIFOCache {
public:
    FIFOCache(uint64_t capacity) : capacity(capacity) {}

    bool get(uint64_t key, CacheItem& item) {
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) return false;

        item = it->second->second;

        return true;
    }

    void put(const CacheItem& item) {
        // 达到容量限制时移除最旧项
        if (cacheMap.size() >= capacity) {
            cacheMap.erase(accessOrder.front().first);
            accessOrder.pop_front();
        }
        // 插入新项到头部
        accessOrder.emplace_back(item.key, item);
        cacheMap[item.key] = -- accessOrder.end();
    }

    bool evict(CacheItem& item) {
        if (cacheMap.empty()) return false;

        item = accessOrder.front().second; // 取出最旧项
        cacheMap.erase(accessOrder.front().first);
        accessOrder.pop_front();
        return true;
    }
    void remove(uint64_t key) {
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            accessOrder.erase(it->second);
            cacheMap.erase(it);
        }
    }

    uint64_t getSize() const { return cacheMap.size(); }
    uint64_t getCapacity() const { return capacity; }

private:
    uint64_t capacity;
    std::list<std::pair<uint64_t, CacheItem>> accessOrder;  // 用于维护 FIFO 顺序
    std::unordered_map<uint64_t, std::list<std::pair<uint64_t, CacheItem>>::iterator> cacheMap; // 用于快速查找
};

#ifdef wtlfu
// WTinyLFU Cache 主类
class WTinyLFUCache {
public:
    WTinyLFUCache(uint64_t windowSize, uint64_t probationSize, uint64_t protectedSize)
        : windowCache(windowSize), probationCache(probationSize), protectedCache(protectedSize) {}

    bool get(uint64_t key, CacheItem& cached_item) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        CacheItem item;

        // 在 protectedCache 中查找
        if (protectedCache.get(key, item)) {
            item.freq++;
            cached_item = item;
            return true;
        }

        // 在 probationCache 中查找，如果存在则将其移至 protectedCache
        if (probationCache.get(key, item)) {
            probationCache.remove(key);
            item.freq++;
            promoteToProtected(item);
            cached_item = item;
            return true;
        }

        // 在 windowCache 中查找，如果存在则将其移至 probationCache
        if (windowCache.get(key, item)) {
            item.freq++;
            cached_item = item;
            return true;
        }

        return false; // 缓存未命中
    }

    bool lock_free_get(uint64_t key, CacheItem& cached_item) {
        CacheItem item;

        // 在 protectedCache 中查找
        if (protectedCache.get(key, item)) {
            item.freq++;
            cached_item = item;
            return true;
        }

        // 在 probationCache 中查找，如果存在则将其移至 protectedCache
        if (probationCache.get(key, item)) {
            item.freq++;
            cached_item = item;
            return true;
        }

        // 在 windowCache 中查找，如果存在则将其移至 probationCache
        if (windowCache.get(key, item)) {
            item.freq++;
            cached_item = item;
            return true;
        }

        return false; // 缓存未命中
    }

    void put(const CacheItem& item) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        CacheItem old_item;

        if (!windowCache.get(item.key, old_item) && !protectedCache.get(item.key, old_item) && !probationCache.get(item.key, old_item)) {
            if (windowCache.getSize() >= windowCache.getCapacity()) {
                CacheItem evictedItem;
                if (windowCache.evict(evictedItem)) {
                    promoteToProbation(evictedItem); // 从 windowCache 移至 probationCache
                }
            }
            windowCache.put(item);
            return;
        }

        return;
    }

    void remove(uint64_t key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        windowCache.remove(key);
        probationCache.remove(key);
        protectedCache.remove(key);
    }
    
    void printfsize() {
        printf("a: %d, b: %d, c: %d\n", windowCache.getSize(), probationCache.getSize(), protectedCache.getSize());
    }

private:
    void promoteToProbation(const CacheItem& item) {
        if (probationCache.getSize() >= probationCache.getCapacity()) {
            CacheItem evictedItem;
            if (probationCache.get_last(evictedItem)) {
                // 比较频率，选择驱逐频率较低的对象
                if (evictedItem.freq < item.freq) {
                    probationCache.evict(evictedItem);
                    probationCache.put(item);
                } else {
                    // probationCache.put(evictedItem); // 保留 evictedItem 在 probationCache
                }
            }
        } else {
            probationCache.put(item);
        }
    }

    void promoteToProtected(const CacheItem& item) {
        if (protectedCache.getSize() >= protectedCache.getCapacity()) {
            CacheItem evictedItem;
            protectedCache.evict(evictedItem);
            promoteToProbation(evictedItem); // 从 protectedCache 移至 probationCache
        }
        protectedCache.put(item);
    }

    LRUCache windowCache;
    LRUCache probationCache;
    LRUCache protectedCache;
    std::mutex cacheMutex;
};
#endif


#ifdef lru
class WTinyLFUCache {
public:
    WTinyLFUCache(uint64_t windowSize, uint64_t probationSize, uint64_t protectedSize)
        : mycache(windowSize+probationSize+protectedSize) {}

    bool get(uint64_t key, CacheItem& cached_item) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        CacheItem item;

        if (mycache.get(key, item)) {
            cached_item = item;
            return true;
        }

        return false; // 缓存未命中
    }

    void put(const CacheItem& item) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        // CacheItem old_item;

        // 检查是否已经在缓存中
        // if (!mycache.get(item.key, old_item)) {
            mycache.put(item);
        // }
    }

    void remove(uint64_t key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        mycache.remove(key);
    }

    void printfsize() {
        printf("a: %d\n", mycache.getSize());
    }

    LRUCache mycache;
    std::mutex cacheMutex;
};

#endif


