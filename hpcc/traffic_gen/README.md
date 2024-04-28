# Traffic Generator
This folder includes the scripts for generating traffic.

## Usage

`python traffic_gen.py --help` for help.

Example:
`python3 traffic_gen.py -c WebSearch.txt -n 320 -l 0.3 -b 100G -t 0.1` generates traffic according to the web search flow size distribution, for 320 hosts, at 30% network load with 100Gbps host bandwidth for 0.1 seconds.

The generate traffic can be directly used by the simulation.

## Traffic format
The first line is the number of flows.

Each line after that is a flow: `<source host> <dest host> 3 <dest port number> <flow size (bytes)> <start time (seconds)>`

**Note:** We removed the `3` and `<dest port number>` from the output, since they were not necessary.

## Flow size distributions
We provide 4 distributions. `WebSearch_distribution.txt` and `FbHdp_distribution.txt` are the ones used in the HPCC paper. `AliStorage2019.txt` are collected from Alibaba's production distributed storage system in 2019. `GoogleRPC2008.txt` are Google's RPC size distribution before 2008.

**Note:** We moved these distribution files under `traffic_distributions`. Generated files by default will be stored under a `gen` directory in this folder.

## Getting FCTs

After the simulation has finished, the Flow Monitor should have generated an `<output-prefix>.xml` files (or multiple `<output-prefix>-<mpi-id>.xml` if using MPI). You can pass the directory of these files along with the prefix names to plot the CDF of the FCTs.

For example, if you have a single output at `<path-to-dir>/my-output.xml`, you can do:
```
python3 get_fct <path-to-dir> my-output
```

Or if you have MPI results from 8 processes:
```
python3 get_fct <path-to-dir> my-output --mpi 8
```

You can also filter ACK flows by passing `--no-ack`.