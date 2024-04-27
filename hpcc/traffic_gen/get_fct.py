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
import seaborn as sns
import matplotlib.pyplot as plt
import xml.etree.cElementTree as ET
from typing import List
from scapy.all import PcapReader, IP, TCP


NUM_PODS = 2
SWITCH_RADIX = 4
NUM_SERVERS = 2
TCP_DST_PORT = 10


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


class MpiPcapReader:
    def __init__(self, dir_path) -> None:
        self.dir_path = dir_path
        self.fcts = {}

    @staticmethod
    def list_pcaps(dir_path):
        ls = list(filter(lambda elem: elem.endswith('.pcap'), os.listdir(dir_path)))
        if len(ls) == 0:
            print("No PCAP files found in the given directory. Aborting")
            sys.exit(-1)

        print(f"Found {len(ls)} PCAP files in {dir_path}")

        return ls
    
    @staticmethod
    def get_host_idx_from_pcap_file_name(pcap_file_name):
        ls = pcap_file_name.split('-')
        return int(ls[-1].replace('.pcap', ''))
    
    def parse_pcap(self, pcap_file_name):
        pcap_file_path = os.path.join(self.dir_path, pcap_file_name)
        pcap_reader = PcapReader(pcap_file_path)
        host_idx = self.get_host_idx_from_pcap_file_name(pcap_file_name)
        print(f'host_idx for {pcap_file_name} == {host_idx}')

        stray_count = 0
        try:
            while True:
                packet = next(pcap_reader)
                # print(f"{packet[IP].src}:{packet[TCP].sport} --> {packet[IP].dst}:{packet[TCP].dport}: {len(packet)}")
                if packet[TCP].dport == TCP_DST_PORT:
                    if not ((map_ip_to_host_idx(packet[IP].src) == host_idx or map_ip_to_host_idx(packet[IP].dst) == host_idx) and TCP in packet):
                        stray_count += 1
                    # print(f'{packet.time}: From host {map_ip_to_host_idx(packet[IP].src)} to {map_ip_to_host_idx(packet[IP].dst)}')
        except StopIteration:
            print("Parsing finished")
        except KeyboardInterrupt:
            print("Aborting execution")
            return -1
        finally:
            print(f"Number of stray packets: {stray_count}")
            pcap_reader.close()

    def parse_all_pcaps(self):
        pcap_file_names = self.list_pcaps(self.dir_path)
        for pcap_file_name in pcap_file_names:
            if self.parse_pcap(pcap_file_name):
                sys.exit(-1)


class FlowMonitorXmlParser:
    def __init__(self, path) -> None:
        self.path = path
        # Map flow id to `(size, flow_completion_time)`
        self.fcts = {}

    @staticmethod
    def parse_time_ns(tm: str):
        if tm.endswith("ns"):
            return float(tm[:-2])
        raise ValueError(tm)
    
    @classmethod
    def parse_flow_files(cls, paths: List[str]):
        all_fcts = []
        for path in paths:
            obj = cls(path)
            obj.parse_flow_file()
            all_fcts.append(obj.get_fcts())

        return all_fcts
    
    @staticmethod
    def plot_cdf(fcts, log_scale=True):
        sns.ecdfplot(
            [1000 * elem[1] for elem in fcts.values()], 
            stat='proportion', 
            log_scale=log_scale
        )
    
    def clear(self):
        self.fcts.clear()

    def get_fcts(self):
        return self.fcts
    
    def parse_flow_file(self):
        self.clear()
        
        context = ET.iterparse(self.path, events=("start", "end"))
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
                        self.fcts[int(attrib['flowId'])] = (
                            int(attrib['rxBytes']),
                            (self.parse_time_ns(attrib['timeLastRxPacket']) - self.parse_time_ns(attrib['timeFirstTxPacket'])) * 1e-9
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
        
        print(f"Parsed {len(self.fcts)} flows")
        

def plot_cdfs(all_fcts, title, legends):
    for i in range(len(all_fcts)):
        FlowMonitorXmlParser.plot_cdf(all_fcts[i])

    plt.ylabel("CDF")
    plt.xlabel("FCT (ms)")
    plt.title(title)
    plt.legend(legends)
    plt.show()


if __name__ == '__main__':
    parser = argparse.ArgumentParser("Parse Flow-Monitor XML outputs or PCAPs")

    parser.add_argument('--xml', nargs='+', help="Path(s) to the Flow-Monitor XML outputs")
    parser.add_argument('--pcap', help="Path to the directory containing PCAP files")

    args = parser.parse_args()
    
    if args.xml:
        all_fcts = FlowMonitorXmlParser.parse_flow_files(args.xml)
        plot_cdfs(all_fcts, "title", ["Low Load", "Medium Load", "High Load"])

    if args.pcap:
        mpi_pcap_reader = MpiPcapReader(args.pcap)
        mpi_pcap_reader.parse_all_pcaps()
