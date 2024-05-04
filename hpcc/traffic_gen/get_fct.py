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

import os
import sys
import argparse
import numpy as np
import seaborn as sns
import matplotlib.pyplot as plt
import xml.etree.cElementTree as ET
from typing import List
from scapy.all import PcapReader, IP, TCP


NUM_PODS = 2
SWITCH_RADIX = 4
NUM_SERVERS = 2

T_START = 1.5
SMALL_FLOW_THRESHOLD = 150 * 1024


def map_ip_to_host_idx(ip):
    ls = ip.split('.')
    assert (len(ls) == 4 and ls[0] == '10')
    pod_num = int(ls[1])
    assert (pod_num < NUM_PODS)
    tor_num = int(ls[2])
    assert (tor_num < SWITCH_RADIX // 2)
    server_num = int(ls[3])
    assert (server_num <= NUM_SERVERS)

    return pod_num * NUM_SERVERS * SWITCH_RADIX // 2 + tor_num * NUM_SERVERS + server_num


class FlowMonitorXmlParser:
    def __init__(self, paths) -> None:
        self.paths = paths
        # Map flow id to `(size, flow_completion_time)`
        self.fcts_small = {}
        # Map flow id to throughput
        self.tps_large = {}
        self.min_start_tx = None
        self.max_last_rx = None

    @staticmethod
    def parse_time_ns(tm: str):
        if tm.endswith("ns"):
            return float(tm[:-2])
        raise ValueError(tm)
    
    def parse_flow_files(self):
        for path in self.paths:
            self.parse_flow_file(path)
                
        print(f"Results for {self.paths[0]}")
        print(f"Parsed {len(self.fcts_small)} small flows")
        print(f"Parsed {len(self.tps_large)} large flows")
        print(f"Flows between {self.min_start_tx} and {self.max_last_rx}")
        print(f"Short flow FCT p99 is {np.percentile([1000 * elem[1] for elem in self.fcts_small.values()], 99)} ms")
        print(f"Long flow TP p1 is {8 * np.percentile([elem[1] for elem in self.tps_large.values()], 1)} bps")
    
    @staticmethod
    def plot_fct_cdf(fcts, log_scale=True):
        sns.ecdfplot(
            [1000 * elem[1] for elem in fcts.values()], 
            stat='proportion', 
            log_scale=log_scale
        )

    @staticmethod
    def plot_tp_cdf(tps, log_scale=True):
        sns.ecdfplot(
            [1000 * elem[1] for elem in tps.values()], 
            stat='proportion', 
            log_scale=log_scale
        )

    def get_fcts(self):
        return self.fcts_small
    
    def get_tps(self):
        return self.tps_large
    
    def parse_flow_file(self, path):
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
                        assert int(attrib['flowId']) not in self.fcts_small
                        assert int(attrib['flowId']) not in self.tps_large
                        # assert self.parse_time_ns(attrib['timeLastRxPacket']) > 0
                        # assert self.parse_time_ns(attrib['timeFirstTxPacket']) > 0
                        t_start = self.parse_time_ns(attrib['timeFirstTxPacket']) * 1e-9
                        t_finish = self.parse_time_ns(attrib['timeLastRxPacket']) * 1e-9

                        if t_start >= T_START:
                            if int(attrib['rxBytes']) < SMALL_FLOW_THRESHOLD:
                                self.fcts_small[int(attrib['flowId'])] = (
                                    int(attrib['rxBytes']),
                                    (t_finish - t_start)
                                )
                            else:
                                self.tps_large[int(attrib['flowId'])] = (
                                    int(attrib['rxBytes']),
                                    int(attrib['rxBytes']) / (t_finish - t_start)
                                )
                            
                            if not self.min_start_tx:
                                self.min_start_tx = t_start
                            else:
                                if self.min_start_tx > t_start:
                                    self.min_start_tx = t_start

                            if not self.max_last_rx:
                                self.max_last_rx = t_finish
                            else:
                                if self.max_last_rx < t_finish:
                                    self.max_last_rx = t_finish
                        
                        level += 1
                    elif parsing_flow_class:
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
        

def plot_cdfs(all_fcts, all_tps):
    plt.subplot(1, 2, 1)
    FlowMonitorXmlParser.plot_fct_cdf(all_fcts)
    plt.ylabel("CDF")
    plt.xlabel("Short flow FCTs (ms)")

    plt.subplot(1, 2, 2)
    FlowMonitorXmlParser.plot_tp_cdf(all_tps)
    plt.ylabel("CDF")
    plt.xlabel("Large flow TPs (Byte/sec)")


if __name__ == '__main__':
    parser = argparse.ArgumentParser("Parse Flow-Monitor XML outputs or PCAPs")

    parser.add_argument('dir', help="Path to the directory containing the flow monitor output")
    parser.add_argument('names', nargs='+', help="Prefix name of the outputs")
    parser.add_argument('--mpi', help="Number of MPI outputs")

    args = parser.parse_args()
    
    if args.mpi:
        for name in args.names:
            paths = []

            for i in range(int(args.mpi)):
                paths.append(os.path.join(args.dir, name + '-' + str(i) + '.xml'))

            flow_monitor_parser = FlowMonitorXmlParser(paths)
            flow_monitor_parser.parse_flow_files()

            plot_cdfs(flow_monitor_parser.get_fcts(), flow_monitor_parser.get_tps())
    else:
        paths = []
        for name in args.names:
            name.replace('.xml', '')
            paths.append(os.path.join(args.dir, name + '.xml'))

            flow_monitor_parser = FlowMonitorXmlParser(paths)
            flow_monitor_parser.parse_flow_files()

            plot_cdfs(flow_monitor_parser.get_fcts(), flow_monitor_parser.get_tps())

    plt.legend(args.names)
    plt.show()
