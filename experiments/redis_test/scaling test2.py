import json
import memcache
import time
import os
import subprocess
import numpy as np
import sys

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
mc.flush_all()

# start redis instance
os.chdir('/home/Nautilus/experiments/redis_test/')

instance_controller_id = 0
memcached_ip = cluster_ips[master_id]
my_server_ip = cluster_ips[master_id]

num_servers = 20
server_port_st = 7000
num_cores = 20

server_ports = [server_port_st + i for i in range(num_servers)]
scale_ports = [server_port_st +
               i for i in range(num_servers // 2, num_servers)]

# clear old settings
for p in server_ports:
    if os.path.exists(f'./{p}'):
        if os.path.exists(f'./{p}/pid'):
            pid = int(open(f'./{p}/pid', 'r').read())
            os.system(f'sudo kill -9 {pid}')
        os.system(f'rm -rf ./{p}')
    os.mkdir(f'./{p}')

# construct configurations
config_templ = open('redis-large.conf.templ', 'r').read()
for p in server_ports:
    with open(f'./{p}/redis.conf', 'w') as f:
        f.write(config_templ.format(p, p, my_server_ip))

# start redis instances
# os.system('ulimit -n unlimited')
for i, p in enumerate(server_ports):
    os.system(f'cd {p}; \
              taskset -c {i % num_cores} redis-server ./redis.conf; cd ..')

mc.set(f'redis-0-ready', 0)
print("Finished creating instances, wait clean-up")

# wait for client loading signal and starts chekcing scale instance logs
val = mc.get('cluster-change')
while val == None:
    time.sleep(1)
    val = mc.get('cluster-change')

# check instance state
for p in scale_ports:
    success = False
    while not success:
        f = open(f'./{p}/log-{p}')
        log = f.readlines()
        for l in log:
            if 'Cluster state changed: ok' in l:
                success = True
                break
    mc.set(f'{my_server_ip}:{p}-success', 1)

# wait for finishing workload
val = mc.get('test-finish')
while val == None:
    time.sleep(1)
    val = mc.get('test-finish')

# clean-up redis instances
for p in server_ports:
    if not os.path.exists(f'./{p}'):
        continue
    if os.path.exists(f'./{p}/pid'):
        pid = int(open(f'./{p}/pid', 'r').read())
        os.system(f'sudo kill -9 {pid}')
    os.system(f'rm -rf ./{p}')




