#include <chrono>
#include "swarm.h"
#include "ns3/flow-monitor-helper.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE(COMPONENT_NAME);


void logDescriptors(topolgoy_descriptor *topo_params) {
    SWARM_INFO("Building FatTree with the following params:");
    SWARM_INFO("\tLink Rate = " << topo_params->linkRate << " Gbps");
    SWARM_INFO("\tLink Delay = " << topo_params->linkDelay << " us");
    SWARM_INFO("\tSwitch Radix = " << topo_params->switchRadix);
    SWARM_INFO("\tNumber of Servers Per Edge = " << topo_params->numServers);
    SWARM_INFO("\tNumber of Pods = " << topo_params->numPods);

    #if NETANIM_ENABLED
    if (topo_params->animate) {
        SWARM_INFO("Will output NetAnim XML file to " + ANIM_FILE_OUTPUT);
    }
    #endif /* NETANIM_ENABLED */

    if (topo_params->switchRadix / 2 < topo_params->numServers) {
        SWARM_WARN("Number of servers exceeds half the radix. This topology is oversubscribed!");
    }

    NS_ASSERT(topo_params->switchRadix >= topo_params->numPods);
}

ClosTopology :: ClosTopology(const topology_descriptor_t m_params) {
    params = m_params;
}

#if MPI_ENABLED
void ClosTopology :: createCoreMPI() {
    if (systemCount == 1) {
        this->coreSwitchesEven.Create(this->params.switchRadix / 2);
        this->coreSwitchesOdd.Create(this->params.switchRadix / 2);
        return;
    }

    // The EVEN cores will get even systemIDs
    for (uint32_t i = 0; i < this->params.switchRadix / 2; i++) {
        this->coreSwitchesEven.Add(CreateObject<Node>((i % (systemCount / 2) * 2)));
    }

    // ODDs get odd systemIDs
    for (uint32_t i = 0; i < this->params.switchRadix / 2; i++) {
        this->coreSwitchesOdd.Add(CreateObject<Node>((i % (systemCount / 2) * 2) + 1));
    }
}
#endif /* MPI_ENABLED */

void ClosTopology :: createTopology() {
    /**
     * In a Clos topology with `n` pods, created with switches of radix `r`:
     *  - The aggregates will reserve `r/2` uplinks to the core, and the core should be 
     *    at least `r` switches, interleaved between each aggregate.
     *    We assume that we have exactly `r` core switches from now on for simplicity.
     * -  The aggregate to edge per pod, should be a bipartite graph K_{r/2},{r/2}/
     * -  The edge will have `r/2` ports left to serve the hosts, although we allow the number
     *    to be variable.
     *    With links having the exact same characteristics, this means that if `numServers` is
     *    more than `r/2`, the topology would be oversubscribed.
     * -  Similarly, we can also only serve at most `r` pods, we won't allow any more than that.
     * 
     * So in total, we will have `r` core switches, serving `2.r` pods, each containing `r`
     * switches. The number of aggregates would be `2.r.r/2 = r^2` and the same for edges.
     * 
     * When MPI is used, we will put all nodes belonging to the same pod in the same LP, and we
     * will interleave the core nodes independently.
    */

    #if MPI_ENABLED
    this->createCoreMPI();
    #else
    // We separate the core to an ODD and EVEN part for easier linking
    this->coreSwitchesEven.Create(this->params.switchRadix / 2);
    this->coreSwitchesOdd.Create(this->params.switchRadix / 2);
    #endif /* MPI_ENABLED */
    
    // We separate aggregate and edges for each pod for easier linking
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    for (uint32_t i = 0; i < this->params.numPods; i++) {
        this->aggSwitches.push_back(NodeContainer(numAggAndEdgeeSwitchesPerPod, (i % systemCount)));
        this->edgeSwitches.push_back(NodeContainer(numAggAndEdgeeSwitchesPerPod, (i % systemCount)));
    }

    // Create servers
    this->createServers();

    // Install the layer 3 stack
    InternetStackHelper internet;
    internet.Install(coreSwitchesEven);
    internet.Install(coreSwitchesOdd);
    for (uint32_t i = 0; i < this->params.numPods; i++) {
        for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
            internet.Install(this->servers[i * numAggAndEdgeeSwitchesPerPod + j]);
        }
    }
    this->installWcmpStack();
}

void ClosTopology :: createLinks() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    
    // All links have the same spec
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(this->params.linkRate) + "Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue(std::to_string(this->params.linkDelay) + "us"));

    // Edge to Aggregate links
    uint32_t src, dst;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
                NodeContainer p2pContainer;

                src = pod_num * (numAggAndEdgeeSwitchesPerPod) + i;
                dst = pod_num * (numAggAndEdgeeSwitchesPerPod) + j;

                p2pContainer.Add(this->edgeSwitches[pod_num].Get(i));
                p2pContainer.Add(this->aggSwitches[pod_num].Get(j));

                this->edgeToAggLinks[std::make_tuple(src, dst)] = p2p.Install(p2pContainer);
            }
        }
    }

    // Aggregate to Core links
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
                NodeContainer p2pContainer;

                src = pod_num * (numAggAndEdgeeSwitchesPerPod) + i;
                p2pContainer.Add(this->aggSwitches[pod_num].Get(i));

                if (src % 2 == 0) {
                    dst = j;
                    p2pContainer.Add(this->coreSwitchesEven.Get(j));
                }
                else {
                    dst = j + this->params.switchRadix / 2;
                    p2pContainer.Add(this->coreSwitchesOdd.Get(j));
                }

                this->aggToCoreLinks[std::make_tuple(src, dst)] = p2p.Install(p2pContainer);
            }
        }
    }

    // Edge to Server links
    this->connectServers();

    // Animate?
    #if NETANIM_ENABLED
    if (this->params.animate) {
        this->setNodeCoordinates();
    }
    #endif /* NETANIM_ENABLED */
}

void ClosTopology :: createServers() {
    // There will be `numServers * switchRadix/2 * numPods` servers in total, so switchRadix^3/4
    //      servers at most.
    // We key them with (pod_num, edge_switch_index) --> NodeContainer
    
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;

    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            SWARM_DEBG("Creating servers for edge index " << edge_idx << " in pod "
                << pod_num << " with system ID " << pod_num % systemCount);
            NodeContainer edgeServers(this->params.numServers, (pod_num % systemCount));
            this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = edgeServers;

            for (uint32_t i = 0; i < this->params.numServers; i++) {
                this->serverApplications.push_back(ApplicationContainer());
                this->addHostToPortMap(pod_num, edge_idx, i);
            }
        }
    }
}

void ClosTopology :: connectServers() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(this->params.linkRate) + "Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue(std::to_string(this->params.linkDelay) + "us"));

    uint32_t tor_index, server_index;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            NetDeviceContainer currentEdgeServerDevices;
            for (uint32_t i = 0; i < this->params.numServers; i++) {
                NodeContainer p2pContainer;
                tor_index = pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx;
                server_index = i + this->params.numServers * tor_index;
                p2pContainer.Add(this->servers[tor_index].Get(i));
                p2pContainer.Add(this->edgeSwitches[pod_num].Get(edge_idx));
                NetDeviceContainer devices = p2p.Install(p2pContainer);
                currentEdgeServerDevices.Add(devices.Get(0));
                this->serverToEdgeLinks[std::make_tuple(tor_index, server_index)] = devices;
            }

            this->serverDevices[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = currentEdgeServerDevices;
        }
    }
}

void ClosTopology :: assignIpsNaive() {
    /**
     * This just makes a little LAN for each link in the network
     * Results in an unnecessarily big routing table, but that may not matter in 
     * this simulation.
    */
    
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4AddressHelper ipv4;

    // First, Edge to Agg
    ipv4.SetBase(naiveIpv4AddressBase, naiveIpv4AddressMask);

    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        NetDeviceContainer currentDevices;
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
                ipv4.Assign(this->edgeToAggLinks[std::make_tuple(
                    pod_num * numAggAndEdgeeSwitchesPerPod + i,
                    pod_num * numAggAndEdgeeSwitchesPerPod + j
                )]);
                ipv4.NewNetwork();
            }
        }
    }

    // Now Core
    for (auto const& map_elem : this->aggToCoreLinks) {
        ipv4.Assign(map_elem.second);
        ipv4.NewNetwork();
    }

    ipv4.SetBase(serverIpv4AddressBase, serverIpv4AddressMask);

    // Finally, Server to Edge link IPs
    uint32_t switch_idx, server_idx;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            switch_idx = pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx;
            Ipv4InterfaceContainer hostInterfaces;
            for (uint32_t i = 0; i < this->params.numServers; i++) {
                server_idx = switch_idx * this->params.numServers + i;
                hostInterfaces.Add(ipv4.Assign(this->serverToEdgeLinks[std::make_tuple(switch_idx, server_idx)]).Get(0));
                ipv4.NewNetwork();
            }
            this->serverInterfaces[switch_idx] = hostInterfaces;
        }
    }
}

void ClosTopology :: assignIpsLan() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4AddressHelper ipv4;
    ipv4.SetBase(lanIpv4AddressBase, lanIpv4AddressMask);

    /**
     * Each of the following will be its own /24 LAN
     *  1. Core to Aggregate links
     *  2. Aggregate to Edge links per pod
     *  3. Edge to Hosts for per pod, per host
     * 
     * This essentially means that:
     *  - Two servers connected to the same edge, will only use that edge
     *  - Any communication between any interface in a pod, remains in that pod
     *  - Any communication between any interface in the core-aggregate region, never enters a pod
    */

    // Server LANs
    NetDeviceContainer currentDevices;
    Ipv4InterfaceContainer currentInterfaces;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            currentDevices = this->serverDevices[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx];
            currentInterfaces = ipv4.Assign(currentDevices);
            ipv4.NewNetwork();

            // Collect the server interfaces for later
            Ipv4InterfaceContainer hostInterfaces;
            for (uint32_t i = 0; i < currentInterfaces.GetN(); i++) {
                if (i % 2 == 0) {
                    hostInterfaces.Add(currentInterfaces.Get(i));
                }
            }
            this->serverInterfaces[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = hostInterfaces;
        }
    }

    // Pod LANs
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        NetDeviceContainer currentDevices;
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
                currentDevices.Add(this->edgeToAggLinks[std::make_tuple(
                    pod_num * numAggAndEdgeeSwitchesPerPod + i,
                    pod_num * numAggAndEdgeeSwitchesPerPod + j
                )]);
            }
        }
        ipv4.Assign(currentDevices);
        ipv4.NewNetwork();
    }

    // Core LAN
    for (uint32_t core_idx = 0; core_idx < this->params.switchRadix; core_idx++) {
        NetDeviceContainer currentDevices;
        for (auto const& map_elem : this->aggToCoreLinks) {
            if (std::get<1>(map_elem.first) == core_idx) {
                currentDevices.Add(map_elem.second);
            }
            ipv4.Assign(currentDevices);
            ipv4.NewNetwork();
        }
    }
}

void ClosTopology :: assignServerIps() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4AddressHelper ipv4;

    /**
     * IPs are assigned as /16 chunks for each pod.
     * Each ToR will have a network of /24 IP chunks.
     * No interface in the fabric will have any IP address.
    */

    uint32_t tor_index;
    char buf[17];
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            // Get the IP base
            snprintf(buf, 17, "10.%u.%u.0", (unsigned short) pod_num, (unsigned short) i);
            tor_index = pod_num * numAggAndEdgeeSwitchesPerPod + i;
            ipv4.SetBase(Ipv4Address(buf), Ipv4Mask("/24"));
            this->serverInterfaces[tor_index] = ipv4.Assign(this->serverDevices[tor_index]);
        }
    }
}

void ClosTopology :: createFabricInterfaces() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;

    /**
     * Assume we have `p` pods, radix of `r` and `n` servers.
     * Each server has just a single interface of index 1 that points to the ToR serving it.
     * For other nodes:
     *  - EDGES: Interfaces 1 .. n go towards the servers and interfaces n+1 ... n+r/2
     *    go towards the aggregation.
     *  - AGGREGATIONS: Interfaces 1 .. r/2 go towards the edges and r/2+1 .. r go to the core.
     *  - CORE: Interfaces 1 .. p go towards the aggregations, interleaved between odd and even
     *    core indices.
    */

    // edge-server interfaces
    Ptr<Ipv4> ipv4;
    uint32_t switch_idx, if_index;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            ipv4 = this->getEdge(pod_num, edge_idx)->GetObject<Ipv4>();
            for (uint32_t i = 0; i < this->params.numServers; i++) {
                switch_idx = pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx;
                if_index = ipv4->AddInterface(this->serverToEdgeLinks[std::make_tuple(
                    switch_idx,
                    this->params.numServers * switch_idx + i
                )].Get(1));

                ipv4->AddAddress(if_index, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("/8")));
                ipv4->SetUp(if_index);
            }
        }
    }

    // agg-edge interfaces
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t agg_idx = 0; agg_idx < numAggAndEdgeeSwitchesPerPod; agg_idx++) {
            for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
                ipv4 = this->getEdge(pod_num, edge_idx)->GetObject<Ipv4>();
                if_index = ipv4->AddInterface(
                    this->edgeToAggLinks[std::make_tuple(
                        pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx,
                        pod_num * numAggAndEdgeeSwitchesPerPod + agg_idx
                    )].Get(0)
                );
                ipv4->AddAddress(if_index, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("/8")));
                ipv4->SetUp(if_index);

                ipv4 = this->getAggregate(pod_num, agg_idx)->GetObject<Ipv4>();
                if_index = ipv4->AddInterface(
                    this->edgeToAggLinks[std::make_tuple(
                        pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx,
                        pod_num * numAggAndEdgeeSwitchesPerPod + agg_idx
                    )].Get(1)
                );
                ipv4->AddAddress(if_index, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("/8")));
                ipv4->SetUp(if_index);
            }
        }
    }

    // agg-core interfaces
    Ptr<Node> node;
    Ptr<NetDevice> device;
    for (auto const& map_elem : this->aggToCoreLinks) {
        for (uint32_t i = 0; i < 2; i++ ) {
            device = map_elem.second.Get(i);
            node = device->GetNode();
            ipv4 = node->GetObject<Ipv4>();
            if_index = ipv4->AddInterface(device);
            ipv4->AddAddress(if_index, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("/8")));
            ipv4->SetUp(if_index);
        }
    }
}

void ClosTopology :: setupServerRouting() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4StaticRoutingHelper staticHelper;
    
    uint32_t switch_idx;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            switch_idx = numAggAndEdgeeSwitchesPerPod * pod_num + edge_idx;
            for (uint32_t i = 0; i < this->params.numServers; i++) {
                Ptr<Node> ptr = this->getHost(switch_idx, i);
                if (!ptr)
                    continue;
                staticHelper.GetStaticRouting(ptr->GetObject<Ipv4>())->AddNetworkRouteTo(
                    Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), 1
                );
            }
        }
    }
}

void ClosTopology :: setupCoreRouting() {
    Ipv4StaticRoutingHelper staticHelper;
    Ptr<Ipv4StaticRouting> routing;
    char buf[17];

    for (uint32_t core_idx = 0; core_idx < this->params.switchRadix; core_idx++) {
        routing = staticHelper.GetStaticRouting(this->getCore(core_idx)->GetObject<Ipv4>());

        for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
            snprintf(buf, 17, "10.%u.0.0", (unsigned char) pod_num);
            routing->AddNetworkRouteTo(Ipv4Address(buf), Ipv4Mask("/16"), pod_num+1);
        }
    }
}

void ClosTopology :: installWcmpStack() {
    InternetStackHelper internetHelper;
    Ipv4ListRoutingHelper listHelper;
    WcmpStaticRoutingHelper wcmpHelper((uint16_t) (this->params.numPods * this->params.switchRadix / 2), wcmp_level_mapper);
    Ipv4StaticRoutingHelper staticHelper;
    
    listHelper.Add(staticHelper, 0);
    listHelper.Add(wcmpHelper, WCMP_ROUTING_PRIORITY);
    internetHelper.SetRoutingHelper(listHelper);

    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        internetHelper.Install(edgeSwitches[pod_num]);
        internetHelper.Install(aggSwitches[pod_num]);
    }
}

void ClosTopology :: doEcmp() {
    /**
     * The main constraint on the routing is that:
     *  - Traffic between two servers under the same ToR, never leaves that ToR
     *  - Traffic between two servers in the same pod, never leaves that pod
     * 
     * To ensure this, we use WCMP wildecard routes and static routes.
    */

    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    WcmpStaticRoutingHelper wcmpHelper;
    Ipv4StaticRoutingHelper staticHelper;
    Ptr<Ipv4StaticRouting> staticRouter;
    Ptr<wcmp::WcmpStaticRouting> wcmpRouter;

    // Edge nodes
    char buf[17];
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            // // Route the ToR lan
            staticRouter = staticHelper.GetStaticRouting(this->getEdge(pod_num, edge_idx)->GetObject<Ipv4>());
            for (uint32_t i = 0; i < this->params.numServers; i++) {
                snprintf(buf, 17, "10.%u.%u.%u", (unsigned char) pod_num, (unsigned char) edge_idx, (unsigned char) i+1);
                staticRouter->AddHostRouteTo(Ipv4Address(buf), i+1, DIRECT_PATH_METRIC);
            }

            // WCMP wildcards
            wcmpRouter = wcmpHelper.GetWcmpStaticRouting(this->getEdge(pod_num, edge_idx)->GetObject<Ipv4>());
            for (uint32_t if_index = this->params.numServers + 1; if_index <= this->params.numServers + numAggAndEdgeeSwitchesPerPod; if_index++) {
                wcmpRouter->AddWildcardRoute(if_index, 1);
            }
        }
    }

    // Aggregate nodes
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t agg_idx = 0; agg_idx < numAggAndEdgeeSwitchesPerPod; agg_idx++) {
            // Route the pod lan
            staticRouter = staticHelper.GetStaticRouting(this->getAggregate(pod_num, agg_idx)->GetObject<Ipv4>());
            for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
                snprintf(buf, 17, "10.%u.%u.0", (unsigned char) pod_num, (unsigned char) i);
                staticRouter->AddNetworkRouteTo(Ipv4Address(buf), Ipv4Mask("/24"), i+1, DIRECT_PATH_METRIC);
            }

            // WCMP wildcards
            wcmpRouter = wcmpHelper.GetWcmpStaticRouting(this->getAggregate(pod_num, agg_idx)->GetObject<Ipv4>());
            for (uint32_t if_index = numAggAndEdgeeSwitchesPerPod + 1; if_index <= this->params.switchRadix; if_index++) {
                wcmpRouter->AddWildcardRoute(if_index, 1);
            }
        }
    }
}

void ClosTopology :: enableAggregateBackupPaths() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    WcmpStaticRoutingHelper wcmpHelper;
    Ptr<wcmp::WcmpStaticRouting> wcmpRouter;

    char buf[17];
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t agg_idx = 0; agg_idx < numAggAndEdgeeSwitchesPerPod; agg_idx++) {
            // Backup route if any of the direct routes go down.
            // This entry is never triggered if the direct paths are up, but if they are not, everything destined
            // for this pod will get pushed to another aggregate in the hope that it will reach its destination.

            wcmpRouter = wcmpHelper.GetWcmpStaticRouting(this->getAggregate(pod_num, agg_idx)->GetObject<Ipv4>());
            snprintf(buf, 17, "10.%u.0.0", (unsigned char) pod_num);
            for (uint32_t if_index = 1; if_index <= numAggAndEdgeeSwitchesPerPod; if_index++) {
                if (if_index == (pod_num+1))
                    continue;
                wcmpRouter->AddNetworkRouteTo(Ipv4Address(buf), Ipv4Mask("/16"), if_index, BACKUP_PATH_METRIC);
            }
        }
    }
}

#if NETANIM_ENABLED
void ClosTopology :: setNodeCoordinates() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;

    // Set mobility model
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(this->coreSwitchesEven);
    mobility.Install(this->coreSwitchesOdd);
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        mobility.Install(this->aggSwitches[pod_num]);
        mobility.Install(this->edgeSwitches[pod_num]);
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            mobility.Install(this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + i]);
        }
    }

    // Create animation interface
    this->anim = new AnimationInterface(ANIM_FILE_OUTPUT);

    // Core switches
    Ptr<Node> node;
    double x_start = -WIDTH / 2;
    double delta_x = WIDTH / (this->params.switchRadix - 1);
    for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
        node = this->coreSwitchesEven.Get(i);
        this->anim->SetConstantPosition(node, x_start + i * delta_x, CORE_Y);
        this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
        this->anim->UpdateNodeDescription(node, "CORE-" + std::to_string(i));

        node = this->coreSwitchesOdd.Get(i);
        this->anim->SetConstantPosition(node, delta_x / 2 + i * delta_x, CORE_Y);
        this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
        this->anim->UpdateNodeDescription(node, "CORE-" + std::to_string(i + numAggAndEdgeeSwitchesPerPod));
    }

    // Aggregate/Edge switches and Servers
    delta_x = WIDTH / (this->params.numPods * numAggAndEdgeeSwitchesPerPod - 1);
    double x_middle;
    uint32_t idx;
    double server_offset = (this->params.numServers - 1) * SERVER_DELTA / 2;
    for (uint32_t pod_num=0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            idx = pod_num * numAggAndEdgeeSwitchesPerPod + i;
            x_middle = x_start + idx * delta_x;

            node = this->aggSwitches[pod_num].Get(i);
            this->anim->SetConstantPosition(node, x_middle, AGG_Y);
            this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
            this->anim->UpdateNodeDescription(node, "AGG-" + std::to_string(idx));

            node = this->edgeSwitches[pod_num].Get(i);
            this->anim->SetConstantPosition(node, x_middle, EDGE_Y);
            this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
            this->anim->UpdateNodeDescription(node, "EDGE-" + std::to_string(idx));

            for (uint32_t j = 0; j < this->params.numServers; j++) {
                node = this->servers[idx].Get(j);
                this->anim->SetConstantPosition(node, x_middle + j * SERVER_DELTA - server_offset, SERVER_Y);
                this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
                this->anim->UpdateNodeDescription(node, "H-" + std::to_string(idx) + "-" + std::to_string(j));
            }
        }
    }
}
#endif /* NETANIM_ENABLED */

void ClosTopology :: echoBetweenHosts(uint32_t client_host, uint32_t server_host, double interval) {
    UdpEchoServerHelper echoServer(UDP_DISCARD_PORT);
    UdpEchoClientHelper echoClient(this->getServerAddress(server_host), UDP_DISCARD_PORT);

    echoClient.SetAttribute("MaxPackets", UintegerValue(100));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(interval)));
    echoClient.SetAttribute("PacketSize", UintegerValue(64));
    
    this->serverApplications[server_host].Add(echoServer.Install(this->getHost(server_host)));
    this->serverApplications[client_host].Add(echoClient.Install(this->getHost(client_host)));
}

void ClosTopology :: unidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate) {
    uint32_t port = this->getNextPort(server_host);
    Ptr<Node> ptr;
    
    if ((ptr = this->getLocalHost(server_host))) {
        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(this->getServerAddress(server_host), port));
        this->serverApplications[server_host].Add(sink.Install(ptr));
        SWARM_DEBG_ALL("Installed sink on " << server_host);
    }

    if ((ptr = this->getLocalHost(client_host))) {
        OnOffHelper onOffClient("ns3::TcpSocketFactory", InetSocketAddress(this->getServerAddress(server_host), port));
        onOffClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onOffClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onOffClient.SetAttribute("DataRate", StringValue(rate));
        onOffClient.SetAttribute("PacketSize", UintegerValue(UDP_PACKET_SIZE_SMALL));
        onOffClient.SetAttribute("MaxBytes", UintegerValue(0));

        this->serverApplications[client_host].Add(onOffClient.Install(ptr));
        SWARM_DEBG_ALL("Installed client on " << client_host);
    }
}

void ClosTopology :: bidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate) {
    uint32_t port = this->getNextPort(server_host);
    Ptr<Node> ptr;

    if ((ptr = this->getLocalHost(server_host))) {
        UdpEchoServerHelper echoServer(port);
        this->serverApplications[server_host].Add(echoServer.Install(ptr));
        SWARM_DEBG_ALL("Installed server on " << server_host);
    }

    if ((ptr = this->getLocalHost(client_host))) {
        OnOffHelper onOffClient("ns3::UdpSocketFactory", InetSocketAddress(this->getServerAddress(server_host), port));
        onOffClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onOffClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onOffClient.SetAttribute("DataRate", StringValue(rate));
        onOffClient.SetAttribute("PacketSize", UintegerValue(UDP_PACKET_SIZE_SMALL));

        this->serverApplications[client_host].Add(onOffClient.Install(ptr));
        SWARM_DEBG_ALL("Installed client on " << client_host);
    }
}

void ClosTopology :: doAllToAllTcp(uint32_t totalNumberOfServers, std::string scream_rate) {
    for (uint32_t i = 0; i < totalNumberOfServers; i++) {
        for (uint32_t j = 0; j < totalNumberOfServers; j++) {
            if (i == j)
                continue;
            this->unidirectionalCbrBetweenHosts(i, j, scream_rate);
        }
    }
}

std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> ClosTopology :: getLinkInterfaceIndices(topology_level src_level, 
    uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) 
{
    NS_ASSERT(src_level < dst_level);

    uint32_t src_if_idx, dst_if_idx;
    Ptr<Node> src, dst;

    if (src_level == EDGE) {
        NS_ASSERT(dst_level == AGGREGATE);
        std::pair<uint32_t, uint32_t> pair_src = this->getPodAndIndex(src_idx);
        std::pair<uint32_t, uint32_t> pair_dst = this->getPodAndIndex(dst_idx);

        NS_ASSERT(pair_src.first == pair_dst.first);

        src_if_idx = this->params.numServers + pair_dst.second + 1;
        dst_if_idx = pair_src.second + 1;
        src = this->getEdge(src_idx);
        dst = this->getAggregate(dst_idx);
    }
    else {
        NS_ASSERT(src_level == AGGREGATE && dst_level == CORE);
        
        if (src_idx % 2 == 0)
            NS_ASSERT(dst_idx < this->params.switchRadix / 2);
        else 
            NS_ASSERT(dst_idx >= this->params.switchRadix / 2);

        std::pair<uint32_t, uint32_t> pair_src = this->getPodAndIndex(src_idx);
        src_if_idx = this->params.switchRadix / 2 + dst_idx + 1;
        dst_if_idx = pair_src.first + 1;
        src = this->getAggregate(src_idx);
        dst = this->getCore(dst_idx);
    }

    return std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t>{
        src, src_if_idx, dst, dst_if_idx
    };
}


void ClosTopology :: doDisableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Disabling interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props));

    std::get<0>(props)->GetObject<Ipv4>()->SetDown(std::get<1>(props));
    std::get<2>(props)->GetObject<Ipv4>()->SetDown(std::get<3>(props));
}

void ClosTopology :: doEnableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Enabling interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props));

    std::get<0>(props)->GetObject<Ipv4>()->SetUp(std::get<1>(props));
    std::get<2>(props)->GetObject<Ipv4>()->SetUp(std::get<3>(props));
}

void ClosTopology :: doChangeBandwidth(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Changing bandwidth on interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props));

    std::get<0>(props)->GetObject<Ipv4>()->GetNetDevice(std::get<1>(props))->SetAttribute("DataRate", ns3::StringValue(dataRateStr));
    std::get<2>(props)->GetObject<Ipv4>()->GetNetDevice(std::get<3>(props))->SetAttribute("DataRate", ns3::StringValue(dataRateStr));
}

void ClosTopology :: doChangeDelay(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Changing delay on interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props));

    std::get<0>(props)->GetObject<Ipv4>()->GetNetDevice(std::get<1>(props))->GetChannel()->SetAttribute("Delay", ns3::StringValue(delayStr));
    std::get<2>(props)->GetObject<Ipv4>()->GetNetDevice(std::get<3>(props))->GetChannel()->SetAttribute("Delay", ns3::StringValue(delayStr));
}

void disableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    topology->doDisableLink(src_level, src_idx, dst_level, dst_idx);
}

void enableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    topology->doEnableLink(src_level, src_idx, dst_level, dst_idx);
}

void changeBandwidth(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr) {
    topology->doChangeBandwidth(src_level, src_idx, dst_level, dst_idx, dataRateStr);
}

void changeDelay(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr) {
    topology->doChangeDelay(src_level, src_idx, dst_level, dst_idx, delayStr);
}

uint16_t podLevelMapper(ns3::Ipv4Address dest, const topology_descriptor_t *topo_params) {
    /**
     * Each WCMP switch will maintain multiple tables for each
     * pod. The pod index can be found by chunking the destination
     * IP address.
     * This function thus should just get the ToR index from the dest.
     * Now, the IP address if of the form `10.p.e.s`, where
     *  - `p` is the pod num, in [0, numPods)
     *  - `e` is edge index, in [0, switchRadix/2)
     *  - `s` is server index, in [0, numServers)
     * 
     * In our WCMP scheme, the level is equal to the pod index.
    */
    uint32_t ipaddr_val = dest.Get();
    uint8_t *p = (uint8_t *)&ipaddr_val;

    return (uint16_t) p[1];
}

void closHostFlowDispatcher(host_flow *flow, const ClosTopology *topo) {
    /**
     * Given a host_flow struct, we should decide how to schedule it on 
     * the current topology.
     * Usually this is simple:
     *  - If needed, pick a free tcp port on the source
     *  - Create an OnOff application with maxPacketSize set to the flow
     *    size.
     *  - Since FlowScheduler should call this, it is OK to immediatelly 
     *    call start after installing the application
     * 
     * This should also be compatible with MPI.
     * All flows will be collected by a packet sink on the destination.
    */

    Ptr<Node> ptr;

    if ((ptr = topo->getLocalHost(flow->src))) {
        OnOffHelper onOffClient("ns3::TcpSocketFactory", InetSocketAddress(topo->getServerAddress(flow->dst), TCP_DISCARD_PORT));    

        onOffClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onOffClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        onOffClient.SetAttribute("DataRate", StringValue(std::to_string(topo->params.linkRate) + "Gbps"));
        onOffClient.SetAttribute("MaxBytes", UintegerValue(flow->size));
        onOffClient.Install(ptr).Start(Time(0));
    }
}

template<typename... Args> void schedule(double t, link_state_change_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

template<typename... Args> void schedule(double t, link_attribute_change_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

void reportTimeProgress(double end) {
    float progress;
    progress = (Simulator::Now().GetSeconds() - APPLICATION_START_TIME) / (end - APPLICATION_START_TIME);
    int pos = PROGRESS_BAR_WIDTH * progress;
    static double delta = (end - APPLICATION_START_TIME) * TICK_PROGRESS_EVERY_WHAT_PERCENT / 100.0;

    std::clog << "[INFO] [";
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
        if (i < pos) std::clog << "=";
        else if (i == pos) std::clog << ">";
        else std::clog << " ";
    }
    std::clog << "] " << int(progress * 100.0) << "% \r";
    std::clog.flush();

    if (progress < 1.0) {
        Simulator::Schedule(ns3::Seconds(delta), reportTimeProgress, end);
    }
    else {
        std::clog << std::endl;
    }
}

void reportFlowProgress(FlowScheduler *flowScheduler) {
    float progress;
    progress = flowScheduler->getNumberOfScheduledFlows() * 1.0 / flowScheduler->getNumFlows();
    int pos = PROGRESS_BAR_WIDTH * progress;

    std::clog << "[INFO] [";
    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
        if (i < pos) std::clog << "=";
        else if (i == pos) std::clog << ">";
        else std::clog << " ";
    }
    std::clog << "] " << int(progress * 100.0) << "%\r";
    std::clog.flush();

    if (progress < 1.0) {
        Simulator::Schedule(ns3::MilliSeconds(CHECK_FLOW_COMPLETION_EVERY_WHAT_MS), reportFlowProgress, flowScheduler);
    }
    else {
        std::clog << std::endl;
    }
}

void DoReportProgress(double end, FlowScheduler *flowSCheduler) {
    if (systemId != 0)
        return;
    
    if (flowSCheduler)
        Simulator::Schedule(Simulator::Now(), reportFlowProgress, flowSCheduler);
    else
        Simulator::Schedule(Simulator::Now(), reportTimeProgress, end);
}

int main(int argc, char *argv[]) {
    topolgoy_descriptor topo_params;
    double end = 4.0;
    std::string flow_file_path = "";
    std::string screamRate = "";
    bool micro = false;
    bool verbose = false;
    bool monitor = false;

    SWARM_SET_LOG_LEVEL(INFO);

    CommandLine cmd(__FILE__);
    cmd.AddValue("linkRate", "Link data rate in Gbps", topo_params.linkRate);
    cmd.AddValue("linkDelay", "Link delay in microseconds", topo_params.linkDelay);
    cmd.AddValue("switchRadix", "Switch radix", topo_params.switchRadix);
    cmd.AddValue("numServers", "Number of servers per edge switch", topo_params.numServers);
    cmd.AddValue("numPods", "Number of Pods", topo_params.numPods);
    cmd.AddValue("podBackup", "Enable backup routes in a pod", topo_params.enableEdgeBounceBackup);
    cmd.AddValue("flow", "Path of the flow file", flow_file_path);
    cmd.AddValue("scream", "Instruct all servers to scream at a given rate for the whole simulation", screamRate);
    cmd.AddValue("end", "When to end simulation", end);
    cmd.AddValue("micro", "Set time resolution to micro-seconds", micro);
    cmd.AddValue("verbose", "Enable debug log outputs", verbose);
    cmd.AddValue("monitor", "Install FlowMonitor on the network", monitor);

    if (micro)
        Time::SetResolution(Time::US);
    else
        Time::SetResolution(Time::NS);

    if (verbose)
        SWARM_SET_LOG_LEVEL(DEBG);

    #if NETANIM_ENABLED
    cmd.AddValue("vis", "Create NetAnim input", topo_params.animate);
    #endif /* NETANIM_ENABLED */

    #if MPI_ENABLED
    cmd.AddValue("mpi", "Enable MPI", topo_params.mpi);
    #endif /* MPI_ENABLED */

    cmd.Parse(argc, argv);

    #if MPI_ENABLED
    // GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::NullMessageSimulatorImpl"));
    GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::DistributedSimulatorImpl"));
    MpiInterface::Enable(&argc, &argv);

    systemId = MpiInterface::GetSystemId();
    systemCount = MpiInterface::GetSize();

    SWARM_INFO("Number of logical processes = " << systemCount);
    #endif /* MPI_ENABLED */

    logDescriptors(&topo_params);

    if (!monitor)
        SWARM_WARN("Flow monitoring is DISABLED");
    
    SWARM_INFO("Creating SWARM topology");

    // First, bind our level mapping function for WCMP
    wcmp_level_mapper = [topo_params](Ipv4Address dest) {
        return podLevelMapper(dest, &topo_params);
    };

    
    ClosTopology nodes = ClosTopology(topo_params);
    nodes.createTopology();
    nodes.createLinks();

    // Assign IP addresses and create fabric interfaces
    nodes.assignServerIps();
    nodes.createFabricInterfaces();

    // Do ECMP
    nodes.setupServerRouting();
    nodes.setupCoreRouting();
    nodes.doEcmp();

    // Enable backup paths on aggregations
    if (topo_params.enableEdgeBounceBackup) {
        SWARM_INFO("Enabling intra-pod backup routes");
        nodes.enableAggregateBackupPaths();
    }

    uint32_t totalNumberOfServers = nodes.params.numPods * nodes.params.switchRadix * nodes.params.numServers / 2;

    SWARM_INFO("Total number of servers " << totalNumberOfServers);


    SWARM_INFO("Starting applications");

    // Do constant all-to-all stream if needed
    if (screamRate.length()) {
        SWARM_INFO("Doing all-to-all TCP scream with a rate of " << screamRate);
        nodes.doAllToAllTcp(totalNumberOfServers, screamRate);
    }

    // Get the flow file
    FlowScheduler *flowScheduler = nullptr;
    if (flow_file_path.length()) {
        // Bind the flow dispatcher function
        host_flow_dispatcher_function = [nodes](host_flow *flow) {
            return closHostFlowDispatcher(flow, &nodes);
        };

        // Install packet sinks on each server
        nodes.installTcpPacketSinks();

        // Create the flow scheduler
        flowScheduler = new FlowScheduler(flow_file_path, host_flow_dispatcher_function);
        flowScheduler->begin();
    }
    
    nodes.startApplications(APPLICATION_START_TIME, end);

    // Setup flow monitor
    Ptr<FlowMonitor> flowMonitor = nullptr;
    if (monitor) {
        FlowMonitorHelper flowMonitorHelper;
        flowMonitor = flowMonitorHelper.InstallAll();
    }

    DoReportProgress(end, flowScheduler);
    
    auto t_start = std::chrono::system_clock::now();

    Simulator::Stop(Seconds(end + QUIET_INTERVAL_LENGTH));
    Simulator::Run();
    Simulator::Destroy();

    auto t_end = std::chrono::system_clock::now();

    std::chrono::duration<float> took = t_end - t_start;

    SWARM_INFO("Run finished. Took " << (std::chrono::duration_cast<std::chrono::milliseconds>(took).count()) / 1000.0 << " s");

    if (monitor)
        flowMonitor->SerializeToXmlFile("swarm-flows.xml", false, false);

    #if MPI_ENABLED
    MpiInterface::Disable();
    #endif

    return 0;
}


