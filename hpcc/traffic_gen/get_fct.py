#!/bin/python3


"""
We'll use this file to get the flow completion times from a FlowMonitor
output (we assume the XML format is used).
Since the flow file can be very large, we should not parse it at once,
we parse it incrementally. 

We output two things:
    - CDF of FCTs as a whole
    - Histogram of FCTs over packet size
"""

import argparse
import seaborn as sns
import matplotlib.pyplot as plt
import xml.etree.cElementTree as ET
from typing import List


def parse_time_ns(tm: str):
    if tm.endswith("ns"):
        return float(tm[:-2])
    raise ValueError(tm)


def parse_flow_file(path: str):
    # Map flow id to `(size, flow_completion_time)`
    fcts = {}

    context = ET.iterparse(path, events=("start", "end"))
    context = iter(context)

    level = 0

    parsing_flow_stats = False
    parsing_flow_class = False

    for event, elem in context:
        tag = elem.tag
        
        if tag == 'Dscp' or tag == 'Ipv6FlowClassifier':
            elem.clear()
            continue

        if event == 'start':
            if level == 0:
                if tag == 'FlowMonitor':
                    level += 1
                else:
                    raise ValueError()
            elif level == 1:
                if tag == 'FlowStats':
                    level += 1
                    parsing_flow_stats = True
                elif tag == 'Ipv4FlowClassifier':
                    level += 1
                    parsing_flow_class = True
                else:
                    raise ValueError()
            elif level == 2:
                assert tag == 'Flow'
                if parsing_flow_stats:
                    attrib = elem.attrib
                    fcts[int(attrib['flowId'])] = (
                        int(attrib['rxBytes']),
                        (parse_time_ns(attrib['timeLastRxPacket']) - parse_time_ns(attrib['timeFirstTxPacket'])) * 1e-9
                    )
                    level += 1
                elif parsing_flow_class:
                    # TODO: Prase flow class if needed
                    level += 1
                    pass
            else:
                raise ValueError()

        elif event == 'end':
            if level == 0:
                raise ValueError()
            elif level == 1:
                assert tag == 'FlowMonitor'
                level -= 1
            elif level == 2:
                if tag == 'FlowStats':
                    parsing_flow_stats = False
                    level -= 1
                elif tag == 'Ipv4FlowClassifier':
                    parsing_flow_class = False
                    level -= 1
                else:
                    raise ValueError()
            elif level == 3:
                assert tag == 'Flow'
                level -= 1
            else:
                raise ValueError()
            
        elem.clear()
    
    print(f"Parsed {len(fcts)} flows")
    return fcts


def parse_flow_files(paths: List[str]):
    all_fcts = [parse_flow_file(path) for path in paths]
    return all_fcts


def plot_cdf(fcts):
    sns.ecdfplot([1000 * elem[1] for elem in fcts.values()], stat='proportion', log_scale=True)


def plot_cdfs(all_fcts, legends):
    for i in range(len(all_fcts)):
        plot_cdf(all_fcts[i])
    plt.legend(legends)
    plt.ylabel("CDF")
    plt.xlabel("FCT (ms)")
    plt.title("Storage Traffic")
    plt.show()


if __name__ == '__main__':
    parser = argparse.ArgumentParser("Parse Flow-Monitor XML outputs to get FCTs")

    parser.add_argument('paths', nargs='+', help="Path(s) to the Flow-Monitor XML output")

    args = parser.parse_args()
    
    all_fcts = parse_flow_files(args.paths)
    plot_cdfs(all_fcts, ["Low Load", "Medium Load", "High Load"])
