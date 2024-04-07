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
After this, you should configure the distribution to install the WCMP library. There are two options:

- `configure` will just build the necessary modules (being `core`, `netanim`, `flow-monitor` and `wcmp`) and enables tests for `wcmp`.
- `configure-mpi` will also enable MPI, provided that the dependencies are enabled. It will not configure tests though.

After this, `build` will build the project in the ns3 directory. After this you can run the project:

```cmd
make configure
make build
./ns3/ns3 run "swarm --help"
```

This can take quite a while to compile.

If you do change the configuration, make sure to re-compile from base by purging the current distribution using `purge` target.

## MPI

Currently, MPI is not functioning. I am trying to integrate it to allow simulation on multiple cores. I will note this once its completed.
