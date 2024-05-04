#!/bin/bash


if [ "$#" -ne 1 ]; then
    echo "Usage: ./sample-all-actions.sh <n>"
    exit 1;
fi

template_no_action="mpiexec -np 32 %s \
    --mpi --podLps=32 --coreLps=8 --offloadAgg \ 
    --numPods=8 --switchRadix=8 --numServers=4 --linkRate=20Gbps --linkDelay=100us \
    --end=10.0 \
    --monitor --noAcks --until=1.0 \
    --flow=traffic$1.txt --out=fct-no-action-$1 --scenario=scenario-no-action.txt"

template_disable_high="mpiexec -np 32 %s \
    --mpi --podLps=32 --coreLps=8 --offloadAgg \ 
    --numPods=8 --switchRadix=8 --numServers=4 --linkRate=20Gbps --linkDelay=100us \
    --end=10.0 \
    --monitor --noAcks --until=1.0 \
    --flow=traffic$1.txt --out=fct-disable-high-$1 --scenario=scenario-disable-high-loss.txt"

template_disable_low="mpiexec -np 32 %s \
    --mpi --podLps=32 --coreLps=8 --offloadAgg \ 
    --numPods=8 --switchRadix=8 --numServers=4 --linkRate=20Gbps --linkDelay=100us \
    --end=10.0 \
    --monitor --noAcks --until=1.0 \
    --flow=traffic$1.txt --out=fct-disable-low-$1 --scenario=scenario-disable-low-loss.txt"

template_disable_both="mpiexec -np 32 %s \
    --mpi --podLps=32 --coreLps=8 --offloadAgg \ 
    --numPods=8 --switchRadix=8 --numServers=4 --linkRate=20Gbps --linkDelay=100us \
    --end=10.0 \
    --monitor --noAcks --until=1.0 \
    --flow=traffic$1.txt --out=fct-disable-both-$1 --scenario=scenario-disable-both.txt"

echo "******** NO ACTION ********"
../ns3/ns3 run --command-template="$template_no_action" swarm

echo "******** HIGH LOSS DOWN ********"
../ns3/ns3 run --command-template="$template_disable_high" swarm

echo "******** LOW LOSS DOWN ********"
../ns3/ns3 run --command-template="$template_disable_low" swarm

echo "******** BOTH LINKS DOWN ********"
./ns3 run --command-template="$template_disable_both" swarm
