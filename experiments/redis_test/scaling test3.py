import json
import memcache
import time
import os
import subprocess
import numpy as np

from cluster_setting import *
from utils.utils import save_res, save_time
from utils.settings import get_cache_config_cmd, get_freq_cache_cmd, get_mn_cpu_cmd, get_make_cmd
#from utils.plots import plot_fig1

# timer
g_st = time.time()

redis_instance_id = 0
redis_client_ids = [1,2]

memcached_ip = cluster_ips[master_id]
num_instance_controllers = 1
instance_ips = [cluster_ips[redis_instance_id]]
num_clients = 80
rebalance_start_time = 200
num_instances = 20
run_time = 3000

work_dir = f"{EXP_HOME}/experiments/redis_test"
ULIMIT_CMD = "ulimit -n unlimited"

# create instance list
initial_instances = [f'{instance_ips[i]}:{j+7000}'
                     for i in range(num_instance_controllers)
                     for j in range(num_instances // num_instance_controllers // 2)]
scale_instances = [f'{instance_ips[i]}:{j+7000}'
                   for i in range(num_instance_controllers)
                   for j in range(num_instances // num_instance_controllers // 2,
                                  num_instances // num_instance_controllers)]
all_instances = initial_instances + scale_instances
assert (len(initial_instances) == len(scale_instances))

scale2target = {scale_instances[i]: initial_instances[i]
                for i in range(len(scale_instances))}

# create memcached controller and clear all contents
mc = memcache.Client([memcached_ip])
assert (mc != None)

# sync ycsb load
print("Wait all clients ready.")
for i in range(1, num_clients + 1):
    ready_msg = f'client-{i}-ready-0'
    val = mc.get(ready_msg)
    while val == None:
        val = mc.get(ready_msg)
    print(ready_msg)
print("Notify Clients to load.")
mc.set('all-client-ready-0', 1)  # clients start loading

# wait all clients load ready and sync their to execute trans
for i in range(1, num_clients + 1):
    ready_msg = f'client-{i}-ready-1'
    val = mc.get(ready_msg)
    while val == None:
        val = mc.get(ready_msg)
    print(ready_msg)
mc.set('all-client-ready-1', 1)  # clients start executing trans

print("Notify all clients start trans")
# sleep a while before start rebalancing
time.sleep(rebalance_start_time)
print('Start rebalance')
st = time.time()
os.system(f'redis-cli --cluster rebalance \
          {initial_instances[0]} --cluster-use-empty-masters')
et = time.time()
print(f"Rebalance finishes in {et - st}s")
rebalance_period = et - st

# === starts shrinking resources
time.sleep(rebalance_start_time)
# get node_id to ip map and slot map
print("Getting node map!")
node_id_ip = {}
ip_node_id = {}
for i in all_instances:
    host = i.split(':')[0]
    port = i.split(':')[1]
    proc = subprocess.Popen(f'redis-cli -h {host} -p {port} \
                            cluster nodes | grep myself',
                            stdout=subprocess.PIPE, shell=True)
    proc.wait()
    output = proc.stdout.read().decode().strip()
    node_id = output.split(' ')[0]
    node_id_ip[node_id] = i
    ip_node_id[i] = node_id
    print(f'{i} {node_id}')

# get node_id_slots
print("Start resharding")
st = time.time()
while True:
    ip_slots = {}
    for i in all_instances:
        proc = subprocess.Popen(f'redis-cli --cluster check {i}\
                                | grep {i}', stdout=subprocess.PIPE,
                                shell=True)
        proc.wait()
        l = proc.stdout.readline().decode().strip()
        num_slots = int(l.split(' ')[6])
        ip_slots[i] = num_slots
        print(f'{i} {num_slots}')

    # check reshard finish
    reshard_finish = True
    for inst in scale_instances:
        if ip_slots[inst] != 0:
            reshard_finish = False
            break
    if reshard_finish:
        break

    # reshard scale nodes to initial nodes
    for inst in scale_instances:
        num_slots = ip_slots[inst]
        if num_slots == 0:
            continue
        target_inst = scale2target[inst]
        reshard_cmd = f'redis-cli --cluster reshard {inst} \
                  --cluster-from {ip_node_id[inst]} \
                  --cluster-to {ip_node_id[target_inst]} \
                  --cluster-slots {num_slots} --cluster-yes > /dev/null 2>&1'
        print(reshard_cmd)
        os.system(reshard_cmd)
        time.sleep(2)
    et1 = time.time()
    shrink_period1 = et1 - st
    print(f"Reshard finishes in {shrink_period1}")
    # remove machines from the cluster
    for inst in scale_instances:
        if ip_slots[inst] == 0:
            continue
        time.sleep(5)
        print(f"Remove {inst}")
        os.system(
            f'redis-cli --cluster del-node {instance_ips[0]}:7000 {ip_node_id[inst]}')
et = time.time()
shrink_period = et - st
print(f"Reshard finishes in {shrink_period}s")

# wait for client finish
res = {}
for i in range(1, num_clients + 1):
    key = f'client-{i}-result-0'
    val = mc.get(key)
    while val == None:
        val = mc.get(key)
    res[i] = json.loads(str(val.decode('utf-8')))
res['rebalance_start_time'] = rebalance_start_time
res['rebalance_end_time'] = rebalance_start_time + rebalance_period
res['shrink_start_time'] = res['rebalance_end_time'] + rebalance_start_time
res['shrink_end_time'] = res['shrink_start_time'] + shrink_period
res['shrink_period_1'] = shrink_period1

# save raw results
save_res('fig-raw', res)

# finishing experiment and exit!
mc.set('test-finish', 1)


