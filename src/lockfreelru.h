#include <atomic>
#include <memory>
#include <optional>
#include <iostream>

template<typename T>
class LockFreeLRUQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;
        std::atomic<Node*> prev;
        
        Node(const T& value) : data(value), next(nullptr), prev(nullptr) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;
    std::atomic<size_t> size;
    const size_t capacity;

public:
    LockFreeLRUQueue(size_t max_size) 
        : head(nullptr), tail(nullptr), size(0), capacity(max_size) {}

    ~LockFreeLRUQueue() {
        Node* current = head.load();
        while (current != nullptr) {
            Node* next = current->next.load();
            delete current;
            current = next;
        }
    }

    bool push(const T& value) {
        Node* new_node = new Node(value);
        
        while (true) {
            size_t current_size = size.load();
            
            if (current_size >= capacity) {
                // Remove least recently used element (tail)
                Node* old_tail = tail.load();
                if (!old_tail) {
                    delete new_node;
                    return false;
                }

                Node* prev_tail = old_tail->prev.load();
                if (prev_tail) {
                    prev_tail->next.store(nullptr);
                    tail.store(prev_tail);
                }
                delete old_tail;
                size.fetch_sub(1);
            }

            Node* current_head = head.load();
            new_node->next.store(current_head);

            if (current_head) {
                current_head->prev.store(new_node);
            }

            if (head.compare_exchange_strong(current_head, new_node)) {
                if (!tail.load()) {
                    tail.store(new_node);
                }
                size.fetch_add(1);
                return true;
            }
        }
    }

    std::optional<T> pop() {
        while (true) {
            Node* current_head = head.load();
            if (!current_head) {
                return std::nullopt;
            }

            Node* next = current_head->next.load();
            if (next) {
                next->prev.store(nullptr);
            } else {
                tail.store(nullptr);
            }

            if (head.compare_exchange_strong(current_head, next)) {
                T value = current_head->data;
                delete current_head;
                size.fetch_sub(1);
                return value;
            }
        }
    }

    bool moveToFront(const T& value) {
        Node* current = head.load();
        while (current) {
            if (current->data == value) {
                // If already at front, do nothing
                if (current == head.load()) {
                    return true;
                }

                // Remove from current position
                Node* prev_node = current->prev.load();
                Node* next_node = current->next.load();

                if (prev_node) {
                    prev_node->next.store(next_node);
                }
                if (next_node) {
                    next_node->prev.store(prev_node);
                }
                if (current == tail.load()) {
                    tail.store(prev_node);
                }

                // Move to front
                while (true) {
                    Node* old_head = head.load();
                    current->next.store(old_head);
                    current->prev.store(nullptr);
                    
                    if (head.compare_exchange_strong(old_head, current)) {
                        if (old_head) {
                            old_head->prev.store(current);
                        }
                        return true;
                    }
                }
            }
            current = current->next.load();
        }
        return false;
    }

    size_t getSize() const {
        return size.load();
    }

    bool isEmpty() const {
        return size.load() == 0;
    }
};