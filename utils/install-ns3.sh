#!/bin/bash

swarm_dir="$(pwd)/.."
mkdir ~/ns3
cd ~/ns3
wget https://www.nsnam.org/releases/ns-allinone-3.41.tar.bz2
tar -xf ns-allinone-3.41.tar.bz2
cd $swarm_dir
ln -s ~/ns3/ns-allinone-3.41/ns-3.41 ns3

echo "IMPORTANT: TO ENSURE GOOD PERFORMANCE, SET MPI_MESSAGE_SIZE TO A LARGE NUMBER (say 10240)"
echo "SEE ns3/src/mpi/model/granted-time-window-interface.h"