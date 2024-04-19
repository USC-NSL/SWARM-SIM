# SWARM-SIM
NS-3 simulation for the SWARM project.

## Building

To biuld the project, you need a recent implementation of ns3 (say, [this one](https://www.nsnam.org/releases/ns-allinone-3.41.tar.bz2)), 
download and unpack it in any place that you want.

After cloning the repository, create a SymLink to the directory of the ns3 binary like the following:
```cmd
git clone https://github.com/USC-NSL/SWARM-SIM.git
cd SWARM-SIM
ln -L <path-to-ns3-directory> ns3
```

After this, you can use the provided MakeFile to build the project.

## Make Targets

The targets `check` and `copy` will check for the SymLink and copy the source code into the ns3 distribution.
After this, you should configure the distribution to install the WCMP library. There are multiple configurations:

- **Minimum:** The most basic configuration which will run on a single core.
- **MPI:** Enables MPI, letting the simulation run on multiple cores.
- **Netanim:** For routing protocol debug, this enables Netanim.
- **All:** Allows both MPI and Netanim, though note that it is not possible to use both of them at the same time.
- **Optimized:** Builds NS-3 with the optimized simulator and MPI. We recommend this if you have no intention of
debugging the routing protocol.

After this, the `build` target will build the project in the ns3 directory.

So for example, the most basic build would be:
```cmd
make configure-min
make build
cd ns3
./ns3 run "swarm"
```

Compiling usually take a bit.

Current optional arguments:

```
Program Options:
    --linkRate:     Link data rate in Gbps [40]
    --linkDelay:    Link delay in microseconds [50]
    --switchRadix:  Switch radix [4]
    --numServers:   Number of servers per edge switch [2]
    --numPods:      Number of Pods [2]
    --podBackup:    Enable backup routes in a pod [false]
    --flow:         Path of the flow file
    --scream:       Instruct all servers to scream at a given rate for the whole simulation
    --end:          When to end simulation [4]
    --micro:        Set time resolution to micro-seconds [false]
    --verbose:      Enable debug log outputs [false]
    --monitor:      Install FlowMonitor on the network [false]
    --mpi:          Enable MPI [false]
```

## Using MPI
In general, if the build supports MPI, it can be run with:
```
./ns3 run --command-template="mpiexec -np <num_lps> %s <swarm-arguments>" swarm
```

For example, to simulate a FatTree with switch radix of 16, 4 pods and 8 servers per ToR switch (which 
gives 256 servers in total):
```
./ns3 run --command-template="mpiexec -np 4 %s --switchRadix=16 --numPods=4 --numServers=8" swarm
```

We recommend that if possible, the number of logical processes should be equal to the number of pods, going higher
would usually give negligible performance boost and lower will degrade performance.

### Debugging Under MPI
We recommend GDB for this. To debug the previous command for example:
```
./ns3 run --command-template="mpiexec -np 4 xterm -hold -e gdb %s" swarm
```

This opens 4 GDB terminals. Set breakpoints as you wish and then run with `run --switchRadix=16 --numPods=4 --numServers=8` or
any other arguments that you wish. NS-3 will wait until all GDB unblocks all processes before proceeding.

## Generating Traffic

We use a traffic generator, borrowed from the HPCC project which can be found under `hpcc` directory.

There are 4 traffic distributions, the `GoogleRPC2008` distribution will create many short flows, while the
others generate larger flows but in lesser numbers. See the README under `hpcc/traffic_gen` for more information.

After generating a flow file, it can be passed to the simulation with the `--flow` option. Even with MPI, simulation
can take a very long time. When the `--monitor` option is passed (by default it is not enabled, since it can complicated
debugging), all flows will be tracked with NS-3 Flow-Monitor API. The result will be available in the `swarm-flow.xml` file
that should be generated after the simulation finishes.

We implemented a basic parser under `hpcc/traffic_gen/get_fct.py` that reads this file and generates Flow Completion Time stats.