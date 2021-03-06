# Python 3 file
import sys
import os
import time
import subprocess
import getopt
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from typing import Any, Callable, List, Optional, TextIO, TypeVar

PathLike = TypeVar("PathLike", str, bytes, os.PathLike)  # Type for file/directory paths

# Plots graph based on files in output_directory
# sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
# print(sys.path)


class StatSetting:
    def __init__(
        self,
        line_beginning: str,
        format_func: Callable[[str], Any],
        name_for_legend: str = None,
    ):
        self.line_beginning = line_beginning
        self.format_func = format_func  # Function to format value read from text file
        if name_for_legend is None:
            name_for_legend = line_beginning
        self.name_for_legend = name_for_legend


def run_from_cmdline(
    output_directory_path: PathLike,
    first_experiment_no: int = 1,
    log_file: Optional[TextIO] = None,
):
    # Run without stat_settings list
    run_from_experiment(output_directory_path, first_experiment_no, log_file)


def run_from_experiment(
    output_directory_path: PathLike,
    first_experiment_no: int = 1,
    log_file: Optional[TextIO] = None,
    stat_settings: Optional[List[StatSetting]] = None,
):
    ipc_line_no = 3  # Indexing start from 0, not 1
    if stat_settings is None:
        # StatSetting line_beginning's: case sensitive, not sensitive to leading whitespace
        stat_settings_prefetch_and_ideal_throttling = [
            StatSetting("IPC", float),
            StatSetting(
                "remote dram avg access latency",
                float,
                name_for_legend="remote dram avg access latency (ns)",
            ),
            StatSetting(
                "remote datamovement queue model avg access latency",
                float,
                name_for_legend="  page queue avg access latency (ns)",
            ),
            StatSetting(
                "remote datamovement2 queue model avg access latency",
                float,
                name_for_legend="  cacheline queue avg access latency (ns)",
            ),
            StatSetting(
                "local dram avg access latency",
                float,
                name_for_legend="local dram avg access latency (ns)",
            ),
            StatSetting(
                "average dram access latency",
                float,
                name_for_legend="avg dram access latency (ns)",
            ),
            StatSetting(
                "remote datamovement max effective bandwidth (GB/s)",
                float,
                name_for_legend="page queue max effective bw (GB/s)",
            ),
            StatSetting(
                "remote datamovement % capped by window size",
                float,
                name_for_legend="  page queue capped by window size (%)",
            ),
            StatSetting(
                "remote datamovement % queue utilization full",
                float,
                name_for_legend="  page queue utilization full (%)",
            ),
            StatSetting(
                "remote datamovement2 max effective bandwidth (GB/s)",
                float,
                name_for_legend="cacheline queue max effective bw (GB/s)",
            ),
            StatSetting(
                "remote datamovement2 % capped by window size",
                float,
                name_for_legend="  cacheline queue capped by window size (%)",
            ),
            StatSetting(
                "remote datamovement2 % queue utilization full",
                float,
                name_for_legend="  cacheline queue utilization full (%)",
            ),
            StatSetting("num page moves", int, name_for_legend="num page moves"),
            StatSetting("num inflight hits", int, name_for_legend="num inflight hits"),
            StatSetting(
                "num local evictions",
                lambda s: int(s) / 1000,
                name_for_legend="local evictions (1000s)",
            ),
            StatSetting("num page prefetches", int),
            # StatSetting("page prefetch not done due to full queue", int,
            #             name_for_legend="prefetch not done: full queue"),
            # StatSetting("page prefetch not done since page local already", int,
            #             name_for_legend="prefetch not done: page local already"),
            # StatSetting("page prefetch not done since page uninitialized/not seen yet", int,
            #             name_for_legend="prefetch not done: page uninitialized/not seen yet"),
            # StatSetting("num redundant moves", int, name_for_legend="num redundant moves total"),
            StatSetting(
                "num redundant moves total",
                int,
                name_for_legend="num redundant moves total",
            ),
            StatSetting(
                "num redundant moves type1",
                int,
                name_for_legend="  num redundant moves type1",
            ),
            StatSetting(
                "num type1 cache slower than page",
                int,
                name_for_legend="    num type1 cache slower than page",
            ),
            StatSetting(
                "num redundant moves type2",
                int,
                name_for_legend="  num redundant moves type2",
            ),
            StatSetting(
                "PQ=1 type1 time savings (ns)",
                float,
                name_for_legend="PQ=1 type1 approx latency savings (ns)",
            ),
            StatSetting(
                "PQ=1 type2 time savings (ns)",
                float,
                name_for_legend="PQ=1 type2 approx latency savings (ns)",
            ),
            # StatSetting(
            #     "ideal page throttling: num swaps inflight",
            #     int,
            #     name_for_legend="ideal page throttling: # swaps inflight",
            # ),
            # StatSetting(
            #     "ideal page throttling: num swaps non-inflight",
            #     int,
            #     name_for_legend="ideal page throttling: # swaps non-inflight",
            # ),
            # StatSetting(
            #     "ideal page throttling: num swaps unavailable",
            #     int,
            #     name_for_legend="ideal page throttling: # swaps unavailable",
            # ),
            StatSetting(
                "remote page move cancelled due to full bufferspace",
                int,
                name_for_legend="remote move cancelled, full bufferspace",
            ),
            StatSetting(
                "remote page move cancelled due to full queue",
                int,
                name_for_legend="remote move cancelled, full queue",
            ), 
            #  StatSetting("DDR page hits", int),
            #  StatSetting("DDR page misses", int),
        ]
        stat_settings_new_pq_implementation = [
            StatSetting("IPC", float),
            StatSetting(
                "remote dram avg access latency",
                float,
                name_for_legend="remote dram avg access latency (ns)",
            ),
            StatSetting(
                "remote both queues total avg access latency",
                float,
                name_for_legend="  both queues total avg access latency (ns)",
            ),
            StatSetting(
                "remote datamovement queue model avg access latency",
                float,
                name_for_legend="    page queue avg access latency (ns)",
            ),
            StatSetting(
                "remote datamovement2 queue model avg access latency",
                float,
                name_for_legend="    cacheline queue avg access latency (ns)",
            ),
            StatSetting(
                "local dram avg access latency",
                float,
                name_for_legend="local dram avg access latency (ns)",
            ),
            StatSetting(
                "average dram access latency",
                float,
                name_for_legend="avg dram access latency (ns)",
            ),
            StatSetting(
                "remote datamovement max effective bandwidth (GB/s)",
                float,
                name_for_legend="remote datamovement max effective bw (GB/s)",
            ),
            StatSetting(
                "page queue max effective bandwidth (GB/s)",
                float,
                name_for_legend="  page queue max effective bandwidth (GB/s)",
            ),
            StatSetting(
                "cacheline queue max effective bandwidth (GB/s)",
                float,
                name_for_legend="  cl queue max effective bandwidth (GB/s)",
            ),
            StatSetting(
                "remote datamovement % capped by window size",
                float,
                name_for_legend="remote datamovement capped by window size (%)",
            ),
            StatSetting(
                "remote datamovement % queue utilization full",
                float,
                name_for_legend="remote datamovement utilization full (%)",
            ),
            StatSetting("PQ=1 page queue avg injected time (ns)", float),
            StatSetting("PQ=1 cacheline queue avg injected time (ns)", float),
            StatSetting("PQ=1 max imbalanced page requests", float),
            StatSetting("PQ=1 max imbalanced cacheline requests", float),
            StatSetting("num page moves", int, name_for_legend="num page moves"),
            StatSetting("num inflight hits", int, name_for_legend="num inflight hits"),
            StatSetting(
                "num local evictions",
                lambda s: int(s) / 1000,
                name_for_legend="local evictions (1000s)",
            ),
            StatSetting("num page prefetches", int),
            # StatSetting("page prefetch not done due to full queue", int,
            #             name_for_legend="prefetch not done: full queue"),
            # StatSetting("page prefetch not done since page local already", int,
            #             name_for_legend="prefetch not done: page local already"),
            # StatSetting("page prefetch not done since page uninitialized/not seen yet", int,
            #             name_for_legend="prefetch not done: page uninitialized/not seen yet"),
            # StatSetting("num redundant moves", int, name_for_legend="num redundant moves total"),
            StatSetting(
                "num redundant moves total",
                int,
                name_for_legend="num redundant moves total",
            ),
            StatSetting(
                "num redundant moves type1",
                int,
                name_for_legend="  num redundant moves type1",
            ),
            StatSetting(
                "num type1 cache slower than page",
                int,
                name_for_legend="    num type1 cache slower than page",
            ),
            StatSetting(
                "num redundant moves type2",
                int,
                name_for_legend="  num redundant moves type2",
            ),
            StatSetting(
                "PQ=1 type1 time savings (ns)",
                float,
                name_for_legend="PQ=1 type1 approx latency savings (ns)",
            ),
            StatSetting(
                "PQ=1 type2 time savings (ns)",
                float,
                name_for_legend="PQ=1 type2 approx latency savings (ns)",
            ),
            # StatSetting(
            #     "ideal page throttling: num swaps inflight",
            #     int,
            #     name_for_legend="ideal page throttling: # swaps inflight",
            # ),
            # StatSetting(
            #     "ideal page throttling: num swaps non-inflight",
            #     int,
            #     name_for_legend="ideal page throttling: # swaps non-inflight",
            # ),
            # StatSetting(
            #     "ideal page throttling: num swaps unavailable",
            #     int,
            #     name_for_legend="ideal page throttling: # swaps unavailable",
            # ),
            StatSetting(
                "remote page move cancelled due to full bufferspace",
                int,
                name_for_legend="remote move cancelled, full bufferspace",
            ),
            StatSetting(
                "remote page move cancelled due to full queue",
                int,
                name_for_legend="remote move cancelled, full queue",
            ), 
            #  StatSetting("DDR page hits", int),
            #  StatSetting("DDR page misses", int),
        ]
        # stat_settings = stat_settings_prefetch_and_ideal_throttling
        stat_settings = stat_settings_new_pq_implementation

    y_value_line_nos = [None for _ in range(len(stat_settings))]
    y_values = [[] for _ in range(len(stat_settings))]

    # x_value_line_no = None
    # config_param_values = []

    first_file = True
    file_num = first_experiment_no
    out_file_path = os.path.join(output_directory_path, "{}_sim.out".format(file_num))
    # Read all the output files, starting from 1 and going up
    while os.path.isfile(out_file_path):
        with open(out_file_path, "r") as out_file:
            out_file_lines = out_file.readlines()
            if len(out_file_lines) == 0:
                # The file is empty...
                print("{} is an empty file!".format(out_file_path))
                for index in range(len(y_values)):
                    y_values[index].append(np.nan)
                file_num += 1
                out_file_path = os.path.join(
                    output_directory_path, "{}_sim.out".format(file_num)
                )
                continue
            if first_file:
                # Get line numbers of relevant lines
                for line_no, line in enumerate(out_file_lines):
                    for index, stat_setting in enumerate(stat_settings):
                        if line.strip().startswith(stat_setting.line_beginning):
                            y_value_line_nos[index] = line_no
                            y_values[index].append(
                                stat_setting.format_func(line.split()[-1])
                                if line.split()[-1] != "|"
                                else np.nan
                            )  # The last entry of the line
                if not out_file_lines[ipc_line_no].strip().startswith("IPC"):
                    raise ValueError(
                        "Error: didn't find desired line starting with '{}' in .out file".format(
                            "IPC"
                        )
                    )
                elif None in y_value_line_nos:
                    error_strs = []
                    for index, value in enumerate(y_value_line_nos):
                        if value is None:
                            error_strs.append(
                                "Error: didn't find desired line starting with '{}' in .out file".format(
                                    stat_settings[index].line_beginning
                                )
                            )
                            y_values[index].append(np.nan)  # ignore missing stats
                    # raise ValueError("\n".join(error_strs))
                    print("\n".join(error_strs))
            else:
                # Read the lines of pertinant information
                for index in range(len(y_values)):
                    if y_value_line_nos[index] is None or not out_file_lines[y_value_line_nos[index]].strip().startswith(stat_settings[index].line_beginning):
                        # Couldn't find the line where it was in the previous file, try looking again for the line in this file
                        y_value_line_nos[index] = None
                        for line_no, line in enumerate(out_file_lines):
                            if line.strip().startswith(stat_settings[index].line_beginning):
                                y_value_line_nos[index] = line_no  # found it
                                break
                    if y_value_line_nos[index] is None:  # Still didn't find the line
                        y_values[index].append(np.nan)  # ignore missing stats
                        continue
                    line = out_file_lines[y_value_line_nos[index]]
                    y_values[index].append(
                        stat_settings[index].format_func(line.split()[-1])
                        if line.split()[-1] != "|"
                        else np.nan
                    )  # The last entry of the line

        # Associated sim.cfg file
        # with open(os.path.join(output_directory_path, "{}_sim.cfg".format(file_num)), "r") as config_file:
        #     if first_file:
        #         for line_no, line in enumerate(config_file.readlines()):
        #             if line.startswith(config_line_beginning):
        #                 x_value_line_no = line_no
        #                 # The entry after the equals sign
        #                 config_param_values.append(float(line.split()[2]))
        #         if x_value_line_no is None:
        #             raise ValueError("Error: didn't find desired line starting with '{}' in .cfg file".format(
        #                 config_line_beginning))
        #     else:
        #         line = config_file.readlines()[x_value_line_no]
        #         # The entry after the equals sign
        #         config_param_values.append(float(line.split()[2]))
        first_file = False
        file_num += 1
        out_file_path = os.path.join(
            output_directory_path, "{}_sim.out".format(file_num)
        )

    save_graph_pq(output_directory_path, y_values, stat_settings, log_file)


def get_list_padded_str(l: List[Any]):
    elements = ['{:>7}'.format(val) for val in l]
    return "[" + ", ".join(elements) + "]"


def save_graph_pq(
    output_directory_path: PathLike,
    y_values: List[List[Any]],
    stat_settings: List[StatSetting],
    log_file: Optional[TextIO] = None,
):
    plt.clf()
    plt.close("all")

    if len(y_values[0]) == 7:
        x_axis = [
            "no\nremote\nmem\n",
            "page\nmove\ninstant",
            "page\nmove\nnet lat\nonly",
            "page\nmove\nbw\nonly",
            "pq0",
            "pq1",
            "page\nmove\n120ns net lat\nonly",
        ]
        pq0_index = 4
    elif len(y_values[0]) == 6:
        x_axis = [
            "no\nremote\nmem\n",
            "page\nmove\ninstant",
            "page\nmove\nnet lat\nonly",
            "page\nmove\nbw\nonly",
            "pq0",
            "pq1",
        ]
        pq0_index = 4
    elif len(y_values[0]) == 4:  # Older experiment config setup
        x_axis = ["no\nremote\nmem\n", "pq0\n0 network\nlatency", "pq0", "pq1"]
        pq0_index = 2
    elif len(y_values[0]) == 16:  # PQ and cacheline combined series (pq_cacheline_combined_series)
        x_axis = [
            "no\nremote\nmem\n",
            "page\nmove\ninstant",
            "page\nmove\nnet lat\nonly",
            "page\nmove\nbw\nonly",
            "pq0",
            "pq1\ncl=\n0.1",
            "pq1\ncl=\n0.15",
            "pq1\ncl=\n0.2",
            "pq1\ncl=\n0.25",
            "pq1\ncl=\n0.3",
            "pq1\ncl=\n0.35",
            "pq1\ncl=\n0.4",
            "pq1\ncl=\n0.45",
            "pq1\ncl=\n0.5",
            "pq1\ncl=\n0.55",
            "pq1\ncl=\n0.6",
        ]
        plt.figure(figsize=(10, 4.8))  # (width, height) in inches
        pq0_index = 4
    elif len(y_values[0]) == 13:  # PQ and cacheline combined series, edited (pq_new_series)
        x_axis = [
            "no\nremote\nmem\n",
            "page\nmove\ninstant",
            "pq0",
            "pq1\ncl=\n0.005",
            "pq1\ncl=\n0.01",
            "pq1\ncl=\n0.025",
            "pq1\ncl=\n0.05",
            "pq1\ncl=\n0.075",
            "pq1\ncl=\n0.1",
            "pq1\ncl=\n0.15",
            "pq1\ncl=\n0.2",
            "pq1\ncl=\n0.3",
            "pq1\ncl=\n0.5",
        ]
        plt.figure(figsize=(9, 4.8))  # (width, height) in inches
        pq0_index = 2
    elif len(y_values[0]) == 12:  # PQ and cacheline combined series, edited (pq_newer_series)
        x_axis = [
            "no\nremote\nmem\n",
            "page\nmove\ninstant",
            "pq0",
            "pq1\ncl=\n0.01",
            "pq1\ncl=\n0.025",
            "pq1\ncl=\n0.05",
            "pq1\ncl=\n0.075",
            "pq1\ncl=\n0.1",
            "pq1\ncl=\n0.15",
            "pq1\ncl=\n0.2",
            "pq1\ncl=\n0.3",
            "pq1\ncl=\n0.5",
        ]
        plt.figure(figsize=(9, 4.8))  # (width, height) in inches
        pq0_index = 2
    elif len(y_values[0]) == 11:  # PQ and cacheline combined series, edited (pq_newer_series), TEMP
        x_axis = [
            "no\nremote\nmem\n",
            "page\nmove\ninstant",
            "pq0",
            "pq1\ncl=\n0.01",
            "pq1\ncl=\n0.025",
            "pq1\ncl=\n0.05",
            "pq1\ncl=\n0.075",
            "pq1\ncl=\n0.1",
            "pq1\ncl=\n0.15",
            "pq1\ncl=\n0.2",
            "pq1\ncl=\n0.3",
        ]
        plt.figure(figsize=(9, 4.8))  # (width, height) in inches
        pq0_index = 2
    elif len(y_values[0]) == 2 or len(y_values[0]) == 3:  # TEMP
        x_axis = [
            "pq0",
            "pq1\ncl=\n0.2",
            "pq1\ncl=\n0.5",
        ]
        if len(y_values[0]) == 2:
            x_axis = x_axis[0:2]
        pq0_index = 0
    else:
        raise ValueError("number of experiment runs={}, inaccurate?".format(len(y_values[0])))
    plt.axhline(y=y_values[0][pq0_index])  # horizontal line at pq0 IPC for easier comparison

    print("{}:".format(os.path.basename(os.path.normpath(output_directory_path))))
    print("X values:\n", [s.replace("\n", " ") for s in x_axis])
    print("Y values:")
    for i, y_value_list in enumerate(y_values):
        print("{:45}: {}".format(stat_settings[i].name_for_legend, get_list_padded_str(y_value_list)))
    print()

    if log_file:  # Also print to log file
        print("X values:\n", [s.replace("\n", " ") for s in x_axis], file=log_file)
        print("Y values:", file=log_file)
        for i, y_value_list in enumerate(y_values):
            print(
                "{:45}: {}".format(stat_settings[i].name_for_legend, get_list_padded_str(y_value_list)),
                file=log_file,
            )
        print(file=log_file)

    # Plot as graph
    line_style_list = [".--r", ".--g", ".--b", ".--c", ".--m", ".--y"]
    if len(line_style_list) < len(y_values):
        line_style_list.extend(
            [".--r" for _ in range(len(y_values) - len(line_style_list))]
        )  # Extend list
    elif len(line_style_list) > len(y_values):
        line_style_list = line_style_list[: len(y_values)]  # Truncate list

    # for i, (y_value_list, line_style) in enumerate(zip(y_values, line_style_list)):
    #     plt.plot(
    #         x_axis, y_value_list, line_style, label=stat_settings[i].name_for_legend
    #     )
    # Temporarily: only plot IPC in the graph
    plt.plot(x_axis, y_values[0], ".--r", label=stat_settings[0].name_for_legend)
    # Uniform scale among experiments for the same application and input
    y_axis_top = 0.55
    if "bcsstk" in output_directory_path:
        y_axis_top = 1.4
    data_y_max = max(y_values[0])
    if data_y_max > y_axis_top:
        y_axis_top = data_y_max + max(0.2, data_y_max * 0.05)
    plt.ylim(bottom=0, top=y_axis_top + 0.04)
    plt.yticks(
        np.arange(0, y_axis_top + 0.01, step=(0.05 if y_axis_top < 0.8 else 0.1))
    )  # +0.01 to include y_axis_top

    title_str = "Effect of Partition Queues"
    plt.title(title_str)
    # plt.xlabel("{}".format(config_param_name))
    plt.ylabel("Stats")
    # plt.axvline(x=70000000)  # For local DRAM size graph
    plt.legend()
    plt.tight_layout()
    # Note: .png files are deleted by 'make clean' in the test/shared Makefile in the original repo
    graph_filename = "{} {}.png".format(os.path.basename(os.path.normpath(output_directory_path)), title_str)
    plt.savefig(os.path.join(output_directory_path, graph_filename))
    # plt.show()

    old_file_path = os.path.join(output_directory_path, "Effect of Partition Queues.png")
    if os.path.isfile(old_file_path):
        os.remove(old_file_path)


if __name__ == "__main__":
    run_from_cmdline(".")

    # output_directory_path = None
    # config_param_category = None
    # config_param_name = None

    # usage_str = 'Usage: python3 plot_graph.py -d <directory of experiment results> -c <config parameter category> -n <config parameter name>'
    # try:
    #     opts, args = getopt.getopt(sys.argv[1:], "hd:c:n:", [
    #                                "directory=", "category=", "name="])
    # except getopt.GetoptError:
    #     print(usage_str)
    #     sys.exit(2)
    # for opt, arg in opts:
    #     if opt == '-h':
    #         print(usage_str)
    #         sys.exit()
    #     elif opt in ("-d", "--directory"):
    #         output_directory_path = arg
    #     elif opt in ("-c", "--category"):
    #         if arg[0] == '[' and arg[-1] == ']':
    #             arg = arg[1:-1]  # Strip enclosing square brackets
    #         config_param_category = arg
    #     elif opt in ("-n", "--name"):
    #         config_param_name = arg

    # if None in (output_directory_path, config_param_category, config_param_name):
    #     print(usage_str)
    #     sys.exit(2)

    # run_from_cmdline(output_directory_path,
    #                  config_param_category, config_param_name)
