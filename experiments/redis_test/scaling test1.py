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

# wait for instance controllers to reply
for i in range(num_instance_controllers):
    ready_msg = f'redis-{i}-ready'
    val = mc.get(ready_msg)
    while val == None:
        val = mc.get(ready_msg)
        print("where is it?")
        time.sleep(1)
    print(ready_msg)

print("success step1.")

# create a redis cluster with initial_instances
cmd = 'redis-cli --cluster create ' \
    + ' '.join(initial_instances) + ' --cluster-yes'
os.system(cmd)

# notify instance controllers to check master change
mc.set('cluster-change', 1)
# add empty masters to the cluster
for i in scale_instances:
    os.system(f'redis-cli --cluster add-node \
              {i} {initial_instances[0]}')
    val = mc.get(f'{i}-success')
    while val == None:
        val = mc.get(f'{i}-success')




