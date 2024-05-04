#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: ./setup-experiments.sh <num_runs>"
    exit 1;
fi

echo "Copying scenarios"
cp ./scenarios/*.txt ../ns3

echo "Creating $1 traffic files (128 hosts, 0.9 load for 5 seconds)"
cd ./../hpcc/traffic_gen/
for ((i=1; i<=$1; i++))
do
   ./traffic_gen.py -c WebSearch.txt -n 128 -l 0.9 -t 5 -b 20G -o ../../ns3/"traffic$i.txt"
done

