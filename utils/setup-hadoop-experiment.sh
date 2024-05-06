#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: ./setup-hadoop-experiments.sh <num_runs>"
    exit 1;
fi

echo "Copying scenarios"
cp ./scenarios/*.txt ../ns3

echo "Creating $1 traffic files (FbHdp.txt, 128 hosts, 0.7 load for 2 seconds)"
cd ./../hpcc/traffic_gen/
for ((i=1; i<=$1; i++))
do
   ./traffic_gen.py -c FbHdp.txt -n 128 -l 0.7 -t 2 -b 20G -o ../../ns3/"traffic-hadoop$i.txt"
done

