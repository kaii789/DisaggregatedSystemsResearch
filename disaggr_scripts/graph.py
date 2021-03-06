import pandas as pd
import numpy as np
import matplotlib as plt
plt.use('Agg')
import os

import sys
sys.path.insert(1, '../../tools')
import sniper_lib

def get_ipc():
    res = sniper_lib.get_results(resultsdir='.')
    results = res['results']
    config = res['config']
    ncores = int(config['general/total_cores'])

    if 'barrier.global_time_begin' in results:
        time0_begin = results['barrier.global_time_begin']
        time0_end = results['barrier.global_time_end']

    if 'barrier.global_time' in results:
        time0 = results['barrier.global_time'][0]
    else:
        time0 = time0_begin - time0_end

    if sum(results['performance_model.instruction_count']) == 0:
        # core.instructions is less exact, but in cache-only mode it's all there is
        results['performance_model.instruction_count'] = results['core.instructions']

    results['performance_model.elapsed_time_fixed'] = [
        time0
        for c in range(ncores)
    ]
    results['performance_model.cycle_count_fixed'] = [
        results['performance_model.elapsed_time_fixed'][c] * results['fs_to_cycles_cores'][c]
        for c in range(ncores)
    ]
    results['performance_model.ipc'] = [
        i / (c or 1)
        for i, c in zip(results['performance_model.instruction_count'], results['performance_model.cycle_count_fixed'])
    ]
    return results['performance_model.ipc'][0]

def run_experiment(x_axis, x_axis_init, result_filename, config_name):
    data = {x_axis:x_axis_init,
            'IPC':[]}

    for val in x_axis_init:
        command = "../../run-sniper -v -n 1 -c gainestown -g --{}={} -- ./demo".format(config_name, val / 4)
        os.system(command)
        ipc = get_ipc()
        print(ipc)
        data['IPC'].append(ipc)

    df = pd.DataFrame(data)
    graph = df.plot(x=x_axis, y="IPC")
    fig = graph.get_figure()
    fig.savefig(result_filename)

# IPC vs Bandwidth
run_experiment("Bandwidth(gb/s)", [4, 8, 16, 32], "./ipc_vs_bandwidth.pdf", "perf_model/dram/per_controller_bandwidth")
