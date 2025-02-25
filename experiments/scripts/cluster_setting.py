cluster_ips = ['192.168.6.xx', '192.168.6.xx', '192.168.6.xx', '192.168.6.xx', '192.168.6.xx', '192.168.6.xx']
master_id = 0
mn_id = 5
client_ids = [0, 1, 2, 3, 4, 5]

default_fc_size = 10*1024*1024

NUM_CLIENT_PER_NODE = 32

EXP_HOME = 'Nautilus'
build_dir = f'{EXP_HOME}/build'
config_dir = f'{EXP_HOME}/experiments'

RESET_CMD = 'pkill -f controller.py && pkill init'
RESET_WORKER_CMD = 'pkill init'
RESET_MASTER_CMD = 'pkill -f controller.py'
