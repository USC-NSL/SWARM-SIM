#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: ./setup-oversub-experiments.sh <num_runs>"
    exit 1;
fi

echo "Copying scenarios"
cp ./oversub-scenarios/*.txt ../ns3

echo "Creating $1 traffic files (1024 hosts, 0.6 load for 5 seconds)"
cd ./../hpcc/traffic_gen/
for ((i=1; i<=$1; i++))
do
   ./traffic_gen.py -c WebSearch.txt -n 1024 -l 0.6 -t 5 -b 20G -o ../../ns3/"traffic-oversub$i.txt"
done

