#!/bin/python3

import os
import sys
import tqdm
import random
import math
import heapq
import argparse
from custom_rand import CustomRand

TRAFFIC_DIST_DIR = "traffic_distributions"
GENERATED_DIR = "gen"
BASE_START_TIME_NS = 1000000000


class Flow:
	def __init__(self, src, dst, size, t):
		self.src, self.dst, self.size, self.t = src, dst, size, t
		
	def __str__(self):
		return "%d %d %d %.9f" % (self.src, self.dst, self.size, self.t)


def translate_bandwidth(b):
	if b == None:
		return None
	if type(b)!=str:
		return None
	if b[-1] == 'G':	# Gbps
		return float(b[:-1])*1e9
	elif b[-1] == 'M':	# Mbps
		return float(b[:-1])*1e6
	elif b[-1] == 'K':	# Kbps
		return float(b[:-1])*1e3
	
	return float(b)


def get_cdf_sampler(fileName) -> CustomRand:
	# read the cdf, save in cdf as [[x_i, cdf_i] ...]
	cdf = []
	path = os.path.join(TRAFFIC_DIST_DIR, fileName)

	with open(path) as file:
		lines = file.readlines()

		for line in lines:
			x,y = map(float, line.strip().split(' '))
			cdf.append([x,y])

	# create a custom random generator, which takes a cdf, and generate number according to the cdf
	return CustomRand(cdf)


def poisson(lam):
	return -math.log(1-random.random())*lam


def list_cdfs():
	print("List of available CDF files:\n")
	for file_name in os.listdir(TRAFFIC_DIST_DIR):
		print(f"\t{file_name}")


def print_manifest(avg, nflow_estimate, nhost, bandwidth, load, t):
	print(f"Generating traffic for {nhost} hosts over {bandwidth} bandwidth with {int(load * 100)}% load\n")
	print(f"\tNumber of flows: {nflow_estimate}")
	print(f"\tAverage interarrival time: {avg}")
	print(f"\tTotal time: {t} seconds\n")


if __name__ == "__main__":
	port = 80
	parser = argparse.ArgumentParser("HPCC Traffic Generator")

	parser.add_argument("-n", "--nhost", help = "number of hosts", type=int, default = "32")
	parser.add_argument("-b", "--bandwidth", help = "the bandwidth of host link (G/M/K), by default 10G", default = "10G")
	parser.add_argument("--list", action='store_true', help="Show available CDF files")
	parser.add_argument("-c", "--cdf", dest = "cdf_file", help = "the file of the traffic size cdf", default = "GoogleRPC2008.txt")
	parser.add_argument("-l", "--load", dest = "load", type=float,
					 help = "the percentage of the traffic load to the network capacity, by default 0.3", default = "0.3")
	parser.add_argument("-t", "--time", dest = "time", help = "the total run time (s), by default 10", default = "10")
	parser.add_argument("-o", "--output", dest = "output", help = "the output file")
	args = parser.parse_args()

	if args.list:
		list_cdfs()
		sys.exit(0)

	nhost = args.nhost
	load = float(args.load)
	bandwidth = translate_bandwidth(args.bandwidth)
	time = float(args.time)*1e9 # translates to ns

	customRand = get_cdf_sampler(args.cdf_file)

	# generate flows
	avg = (customRand.get_avg() * 8)		# Average is in bytes!
	avg_inter_arrival = (avg / (bandwidth * load)) * 1e9
	n_flow_estimate = int(time / avg_inter_arrival * nhost)
	n_flow = 0

	print_manifest(avg, n_flow_estimate, nhost, args.bandwidth, load, float(args.time))

	name = args.cdf_file.replace(".txt", "")
	output = args.output if args.output else os.path.join(GENERATED_DIR, f"{nhost}_{args.bandwidth}_{load}_{args.time}_{name}.txt")

	pbar = tqdm.tqdm(total=n_flow_estimate)
	count = 0
	PBAR_STEP = 100
	with open(output, "w") as ofile:
		ofile.write("%d\n"%n_flow_estimate)
		host_list = [(BASE_START_TIME_NS + int(poisson(avg_inter_arrival)), i) for i in range(nhost)]
		heapq.heapify(host_list)
		
		batch = list()
		while len(host_list) > 0:
			t, src = host_list[0]
			inter_t = int(poisson(avg_inter_arrival))
			dst = random.randint(0, nhost-1)
			
			while (dst == src):
				dst = random.randint(0, nhost-1)
			
			if (t + inter_t > time + BASE_START_TIME_NS):
				heapq.heappop(host_list)
			else:
				size = int(customRand.rand())
				if size <= 0:
					size = 1
				n_flow += 1
				ofile.write("%d %d %d %.9f\n" % (src, dst, size, t * 1e-9))
				heapq.heapreplace(host_list, (t + inter_t, src))
			
			count += 1
			if (count % PBAR_STEP == 0):
				pbar.update(PBAR_STEP)
				count = 0
		pbar.close()
		ofile.seek(0)
		ofile.write("%d"%n_flow)
