#include <chrono>
#include <thread>
#include <sys/stat.h>
#include "swarm.h"
#include "ns3/single-flow-helper.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/mpi-flow-monitor-helper.h"


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
    uint32_t numCores = this->params.switchRadix * this->params.switchRadix / 4;
    if (!this->params.mpi) {
        this->coreSwitches.Create(numCores);
        return;
    }

    // For MPI, color cores sequentially
    for (uint32_t i = 0; i < numCores; i++) {
        this->coreSwitches.Add(CreateObject<Node>(i % systemCount));
    }
}
#endif /* MPI_ENABLED */

void ClosTopology :: createTopology() {
    /**
     * In a Clos topology with `n` pods, created with switches of radix `r`:
     *  - The aggregates will reserve `r/2` uplinks to the core, and the core should be 
     *    at least `n * r / 4` switches, interleaved between each aggregate.
     *    We assume that we have exactly `r^2/4` core switches from now on for simplicity.
     * -  The aggregate to edge per pod, should be a bipartite graph K_{r/2},{r/2}/
     * -  The edge will have `r/2` ports left to serve the hosts, although we allow the number
     *    to be variable.
     *    With links having the exact same characteristics, this means that if `numServers` is
     *    more than `r/2`, the topology would be oversubscribed.
     * -  Similarly, we can also only serve at most `r` pods, we won't allow any more than that,
     *    since at that point this is no longer a Clos topology.
     * 
     * So in total, we will have `r^2/4` core switches, serving `r` pods at most, each containing `r`
     * switches. The number of aggregates would be `r.r/2 = r^2/2` and the same for edges.
     * 
     * When MPI is used, we will put all nodes belonging to the same pod in the same LP, and we
     * will interleave the core nodes independently.
     * 
     * If super-mpi is to be used, then each pod would be split in half, to be run on 2 MPI
     * proceses, so in the first case, for `n` pods, there will be at most `n` MPI processes
     * or `2*n` with super-mpi. Note that super-mpi is much more memory hungry. 
    */

    #if MPI_ENABLED
    this->createCoreMPI();
    #else
    uint32_t numCores = this->params.switchRadix * this->params.switchRadix / 4;
    this->coreSwitches.Create(numCores);
    #endif /* MPI_ENABLED */
    
    // We separate aggregate and edges for each pod for easier linking
    uint32_t numAggAndEdgeSwitchesPerPod = this->params.switchRadix / 2;

    if (param_super_mpi)
        NS_ASSERT(this->params.switchRadix % 4 == 0);

    for (uint32_t i = 0; i < this->params.numPods; i++) {
        if (param_super_mpi) {
            NodeContainer aggs, edges;
            aggs.Add(NodeContainer(numAggAndEdgeSwitchesPerPod / 2, ((2*i) % systemCount)));
            edges.Add(NodeContainer(numAggAndEdgeSwitchesPerPod / 2, ((2*i) % systemCount)));
            aggs.Add(NodeContainer(numAggAndEdgeSwitchesPerPod / 2, ((2*i + 1) % systemCount)));
            edges.Add(NodeContainer(numAggAndEdgeSwitchesPerPod / 2, ((2*i + 1) % systemCount)));
            this->aggSwitches.push_back(aggs);
            this->edgeSwitches.push_back(edges);
        }
        else {
            this->aggSwitches.push_back(NodeContainer(numAggAndEdgeSwitchesPerPod, i % systemCount));
            this->edgeSwitches.push_back(NodeContainer(numAggAndEdgeSwitchesPerPod, i % systemCount));
        }
    }

    // Create servers
    this->createServers();

    // Install the layer 3 stack
    InternetStackHelper internet;
    internet.Install(coreSwitches);
    for (uint32_t i = 0; i < this->params.numPods; i++) {
        for (uint32_t j = 0; j < numAggAndEdgeSwitchesPerPod; j++) {
            internet.Install(this->servers[i * numAggAndEdgeSwitchesPerPod + j]);
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

    // Aggregate to Core links, each agggregate links to the core within r/2 steps
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
                NodeContainer p2pContainer;

                src = pod_num * (numAggAndEdgeeSwitchesPerPod) + i;
                dst = i + numAggAndEdgeeSwitchesPerPod * j;
                p2pContainer.Add(this->aggSwitches[pod_num].Get(i));
                p2pContainer.Add(this->coreSwitches.Get(dst));

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
            if (param_super_mpi) {
                if (edge_idx < numAggAndEdgeeSwitchesPerPod / 2) {
                    NodeContainer edgeServers(this->params.numServers, ((2 * pod_num) % systemCount));
                    this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = edgeServers;
                }
                else {
                    NodeContainer edgeServers(this->params.numServers, ((2 * pod_num + 1) % systemCount));
                    this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = edgeServers;
                }
            }
            else {
                NodeContainer edgeServers(this->params.numServers, (pod_num % systemCount));
                this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = edgeServers;
            }

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
     *  - CORE: Each core gets one link to each pod, specifically to the aggregation with index
     *    equal to the core index modulo r/2.
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
    uint32_t numCores = this->params.switchRadix * this->params.switchRadix / 4;
    char buf[17];

    for (uint32_t core_idx = 0; core_idx < numCores; core_idx++) {
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

    if (param_plain_ecmp)
        wcmpHelper.doEcmp();

    if (param_use_cache)
        wcmpHelper.useCache();
    
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

void ClosTopology :: mitigateEdgeToAggregateLink(uint32_t ei, uint32_t aj, uint16_t weight) {
    /**
     * When an edge-aggregate link (say between e_i and a_j) goes down, 
     * two sets of mitigations must be done on WCMP to ensure no packet drops 
     * happen:
     *  - All edges in the same pod as e_i, should set the sending weight of e_i
     *    that points to a_j to zero.
     *  - All edges in other pods, should change the sending weight of e_i
     *    that points to all aggregatation routers that are peers with a_j to zero.
     * 
     * The set of core routers serving a_j is easily found from its index, if the a_j
     * index within the pod is a'_j, then core routers serving it will be the set of
     * cores a'_j + k*r/2 for k = 0, 1, ..., r/2
     * 
     * Note: The peer of an aggregation router is any other aggregation router that
     * connects to the same set of cores. For a Clos topology, that just means that 
     * they have the same index withn the pod, or equivalently, their total indices
     * are r/2 apart.
    */

    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    uint32_t ei_pod_num = this->getPodNum(ei);
    NS_ASSERT(ei_pod_num == this->getPodNum(aj));

    // Update the edges in the same pod
    for (uint32_t k = 0; k < numAggAndEdgeeSwitchesPerPod; k++) {
        uint32_t ek = ei_pod_num * numAggAndEdgeeSwitchesPerPod + k;
        if (ek == ei)
            continue;
        std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = 
            this->getLinkInterfaceIndices(EDGE, ek, AGGREGATE, aj);
        doUpdateWcmp(EDGE, ek, std::get<1>(props), ei, weight);
    }

    // Update the edges in other pods
    uint32_t node_idx_edge, node_idx_agg;
    
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        if (pod_num == ei_pod_num)
            continue;
        
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            node_idx_edge = pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx;
            for (uint32_t agg_idx = 0; agg_idx < numAggAndEdgeeSwitchesPerPod; agg_idx++) {
                if (agg_idx != (aj % numAggAndEdgeeSwitchesPerPod))
                    continue;

                node_idx_agg = pod_num * numAggAndEdgeeSwitchesPerPod + agg_idx;

                std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = 
                    this->getLinkInterfaceIndices(EDGE, node_idx_edge, AGGREGATE, node_idx_agg);

                doUpdateWcmp(EDGE, node_idx_edge, std::get<1>(props), ei, weight);
            }
        }
    }
}

void ClosTopology :: mitigateEdgeToAggregateLinkDown(uint32_t ei, uint32_t aj) {
    this->mitigateEdgeToAggregateLink(ei, aj, 0);
}

void ClosTopology :: mitigateEdgeToAggregateLinkUp(uint32_t ei, uint32_t aj) {
    this->mitigateEdgeToAggregateLink(ei, aj, 1);
}

void ClosTopology :: mitigateAggregateToCoreLink(uint32_t ai, uint32_t cj, uint16_t weight) {
    /**
     * When an aggregation-core link, say between ai and cj goes down, 
     * all aggregations in other pods should set the sending weight of 
     * all edges in the pod of ai that points to cj to 0.
    */

    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    uint32_t ai_pod_num = this->getPodNum(ai);
    uint32_t node_idx, edge_idx;

    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        if (pod_num == ai_pod_num)
            continue;

        for (uint32_t agg_idx = 0; agg_idx < numAggAndEdgeeSwitchesPerPod; agg_idx++) {
            node_idx = pod_num * numAggAndEdgeeSwitchesPerPod + agg_idx;
            if ((node_idx % 2) != (ai % 2))
                continue;

            std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = 
                this->getLinkInterfaceIndices(AGGREGATE, node_idx, CORE, cj);
            
            for (uint32_t k = 0; k < numAggAndEdgeeSwitchesPerPod; k++) {
                edge_idx = numAggAndEdgeeSwitchesPerPod * ai_pod_num + k;    
                doUpdateWcmp(AGGREGATE, node_idx, std::get<1>(props), edge_idx, weight);
            }
        }
    }
}

void ClosTopology :: mitigateAggregateToCoreLinkDown(uint32_t ai, uint32_t cj) {
    this->mitigateAggregateToCoreLink(ai, cj, 0);
}

void ClosTopology :: mitigateAggregateToCoreLinkUp(uint32_t ai, uint32_t cj) {
    this->mitigateAggregateToCoreLink(ai, cj, 1);
}

void ClosTopology :: mitigateLinkDown(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    if (src_level == EDGE) {
        NS_ASSERT(dst_level == AGGREGATE);
        mitigateEdgeToAggregateLinkDown(src_idx, dst_idx);
    }
    else {
        NS_ASSERT(src_level == AGGREGATE && dst_level == CORE);
        mitigateAggregateToCoreLinkDown(src_idx, dst_idx);
    }
}

void ClosTopology :: mitigateLinkUp(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    if (src_level == EDGE) {
        NS_ASSERT(dst_level == AGGREGATE);
        mitigateEdgeToAggregateLinkUp(src_idx, dst_idx);
        restoreStaticRoutesAggregate(dst_idx);
    }
    else if (src_level == AGGREGATE) {
        NS_ASSERT(src_level == AGGREGATE && dst_level == CORE);
        mitigateAggregateToCoreLinkUp(src_idx, dst_idx);
        restoreStaticRoutesCore(dst_idx);
    }
}

void ClosTopology :: restoreStaticRoutesAggregate(uint32_t agg_idx) {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4StaticRoutingHelper staticHelper;
    Ptr<Ipv4StaticRouting> staticRouter;
    
    char buf[17];
    staticRouter = staticHelper.GetStaticRouting(this->getAggregate(agg_idx)->GetObject<Ipv4>());
    for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
        snprintf(buf, 17, "10.%u.%u.0", (unsigned char) getPodNum(agg_idx), (unsigned char) i);
        staticRouter->AddNetworkRouteTo(Ipv4Address(buf), Ipv4Mask("/24"), i+1, DIRECT_PATH_METRIC);
    }
}

void ClosTopology :: restoreStaticRoutesCore(uint32_t core_idx) {
    Ipv4StaticRoutingHelper staticHelper;
    Ptr<Ipv4StaticRouting> routing;
    char buf[17];

    routing = staticHelper.GetStaticRouting(this->getCore(core_idx)->GetObject<Ipv4>());

    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        snprintf(buf, 17, "10.%u.0.0", (unsigned char) pod_num);
        routing->AddNetworkRouteTo(Ipv4Address(buf), Ipv4Mask("/16"), pod_num+1);
    }
}

#if NETANIM_ENABLED

void ClosTopology :: setNodeCoordinates() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;

    // Set mobility model
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(this->coreSwitches);
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        mobility.Install(this->aggSwitches[pod_num]);
        mobility.Install(this->edgeSwitches[pod_num]);
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            mobility.Install(this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + i]);
        }
    }

    // Create animation interface
    #if MPI_ENABLED
    if (this->params.mpi) {
        NS_ABORT_MSG("NetAnim cannot be used with MPI with its current implementation");
    }
    else 
        this->anim = new AnimationInterface(ANIM_FILE_OUTPUT);
    #else
    this->anim = new AnimationInterface(ANIM_FILE_OUTPUT);
    #endif

    // Core switches
    Ptr<Node> node;
    // color color;
    uint32_t numCores = this->params.switchRadix * this->params.switchRadix / 4;
    double x_start = -WIDTH / 2;
    double delta_x = WIDTH / (numCores - 1);
    for (uint32_t i = 0; i < numCores; i++) {
        node = this->coreSwitches.Get(i);
        this->anim->SetConstantPosition(node, x_start + i * delta_x, CORE_Y);
        this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
        this->anim->UpdateNodeDescription(node, "CORE-" + std::to_string(i));

        // getColor(i%4, &color);
        // this->anim->UpdateNodeColor(node, color.r, color.g, color.b);
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

            // getColor(pod_num%4, &color);
            // this->anim->UpdateNodeColor(node, color.r, color.g, color.b);
            node = this->edgeSwitches[pod_num].Get(i);
            this->anim->SetConstantPosition(node, x_middle, EDGE_Y);
            this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
            this->anim->UpdateNodeDescription(node, "EDGE-" + std::to_string(idx));

            // getColor(pod_num%4, &color);
            // this->anim->UpdateNodeColor(node, color.r, color.g, color.b);
            for (uint32_t j = 0; j < this->params.numServers; j++) {
                node = this->servers[idx].Get(j);
                this->anim->SetConstantPosition(node, x_middle + j * SERVER_DELTA - server_offset, SERVER_Y);
                this->anim->UpdateNodeSize(node, NODE_SIZE, NODE_SIZE);
                this->anim->UpdateNodeDescription(node, "H-" + std::to_string(idx) + "-" + std::to_string(j));
                // getColor(pod_num%4, &color);
                // this->anim->UpdateNodeColor(node, color.r, color.g, color.b);
            }
        }
    }
}
#endif /* NETANIM_ENABLED */

void ClosTopology :: echoBetweenHosts(uint32_t client_host, uint32_t server_host, double interval) {
    Ptr<Node> ptr;
    
    if ((ptr = this->getLocalHost(server_host))) {
        UdpEchoServerHelper echoServer(UDP_DISCARD_PORT);
        this->serverApplications[server_host].Add(echoServer.Install(ptr));
    }

    if ((ptr = this->getLocalHost(client_host))) {
        UdpEchoClientHelper echoClient(this->getServerAddress(server_host), UDP_DISCARD_PORT);
        echoClient.SetAttribute("MaxPackets", UintegerValue(1));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(interval)));
        echoClient.SetAttribute("PacketSize", UintegerValue(64));
        
        this->serverApplications[client_host].Add(echoClient.Install(ptr));
    }
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
        NS_ASSERT((src_idx % (this->params.switchRadix / 2)) == (dst_idx % (this->params.switchRadix / 2)));

        std::pair<uint32_t, uint32_t> pair_src = this->getPodAndIndex(src_idx);
        src_if_idx = (dst_idx / (this->params.switchRadix / 2)) + 1;
        dst_if_idx = pair_src.first + 1;
        src = this->getAggregate(src_idx);
        dst = this->getCore(dst_idx);
    }

    return std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t>{
        src, src_if_idx, dst, dst_if_idx
    };
}


void ClosTopology :: doDisableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, 
    uint32_t dst_idx, bool auto_mitiagate) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Disabling interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props));

    std::get<0>(props)->GetObject<Ipv4>()->SetDown(std::get<1>(props));
    std::get<2>(props)->GetObject<Ipv4>()->SetDown(std::get<3>(props));

    if (auto_mitiagate) {
        mitigateLinkDown(src_level, src_idx, dst_level, dst_idx);
    }
}

void ClosTopology :: doEnableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, 
    uint32_t dst_idx, bool auto_mitiagate) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Enabling interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props));

    std::get<0>(props)->GetObject<Ipv4>()->SetUp(std::get<1>(props));
    std::get<2>(props)->GetObject<Ipv4>()->SetUp(std::get<3>(props));

    if (auto_mitiagate) {
        mitigateLinkUp(src_level, src_idx, dst_level, dst_idx);
    }
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

void ClosTopology :: doUpdateWcmp(topology_level node_level, uint32_t node_idx, uint32_t interface_idx, uint16_t level, uint16_t weight) {
    Ptr<Node> node;
    if (node_level == EDGE)
        node = this->getEdge(node_idx);
    else if (node_level == AGGREGATE)
        node = this->getAggregate(node_idx);
    else
        node = this->getCore(node_idx);

    WcmpStaticRoutingHelper wcmp;
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    wcmp.SetInterfaceWeight(ipv4, interface_idx, level, weight);
}

void ClosTopology :: doSetLinkLoss(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string packetLossRate) {
    std::tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> props = this->getLinkInterfaceIndices(
        src_level, src_idx, dst_level, dst_idx
    );

    SWARM_DEBG_ALL("Setting packet drop rate on interfaces " << src_level << ":" << src_idx << ":" << std::get<1>(props)
        << " ---- " << dst_level << ":" << dst_idx << ":" << std::get<3>(props) << " to " << packetLossRate);

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetRate(atof(packetLossRate.c_str()));
    em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);

    std::get<0>(props)->GetObject<Ipv4>()->GetNetDevice(std::get<1>(props))->SetAttribute("ReceiveErrorModel", PointerValue(em));
    std::get<2>(props)->GetObject<Ipv4>()->GetNetDevice(std::get<3>(props))->SetAttribute("ReceiveErrorModel", PointerValue(em));
}

void disableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, 
    uint32_t dst_idx, bool auto_mitiagate) {
    topology->doDisableLink(src_level, src_idx, dst_level, dst_idx, auto_mitiagate);
}

void enableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, 
    uint32_t dst_idx, bool auto_mitiagate) {
    topology->doEnableLink(src_level, src_idx, dst_level, dst_idx, auto_mitiagate);
}

void changeBandwidth(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr) {
    topology->doChangeBandwidth(src_level, src_idx, dst_level, dst_idx, dataRateStr);
}

void changeDelay(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr) {
    topology->doChangeDelay(src_level, src_idx, dst_level, dst_idx, delayStr);
}

void updateWcmp(ClosTopology *topology, topology_level node_level, uint32_t node_idx, uint32_t interface_idx, uint16_t level, uint16_t weight) {
    topology->doUpdateWcmp(node_level, node_idx, interface_idx, level, weight);
}

void setLossRate(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string packetLossRate) {
    topology->doSetLinkLoss(src_level, src_idx, dst_level, dst_idx, packetLossRate);
}

void migrateTraffic(FlowScheduler *flow_scheduler, uint32_t migration_source, uint32_t migration_destination, int percent) {
    NS_ASSERT(percent <= 100 && percent >= -100);
    if (percent > 0) 
        flow_scheduler->migrateTo(migration_source, migration_destination, (uint8_t) percent);
    else
        flow_scheduler->migrateBack(migration_source, migration_destination, (uint8_t) (-percent));
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
     * In this WCMP scheme, the level is equal to the pod index.
    */
    uint32_t ipaddr_val = dest.Get();
    uint8_t *p = (uint8_t *)&ipaddr_val;

    return (uint16_t) p[1];
}

uint16_t torLevelMapper(ns3::Ipv4Address dest, const topology_descriptor_t *topo_params) {
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
     * In this WCMP scheme, the level is equal to the tor index.
    */
    uint32_t ipaddr_val = dest.Get();
    uint8_t *p = (uint8_t *)&ipaddr_val;

    return (uint16_t) p[1] * topo_params->switchRadix/2 + p[2];
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
        SingleFlowHelper singleFlowClient("ns3::TcpSocketFactory", InetSocketAddress(topo->getServerAddress(flow->dst), TCP_DISCARD_PORT));

        singleFlowClient.SetAttribute("DataRate", StringValue(std::to_string(topo->params.linkRate) + "Gbps"));
        singleFlowClient.SetAttribute("PacketSize", UintegerValue(TCP_PACKET_SIZE));
        singleFlowClient.SetAttribute("FlowSize", UintegerValue(flow->size));
        singleFlowClient.Install(ptr).Start(Time(0));
    }
}

template<typename... Args> void schedule(double t, link_state_change_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

template<typename... Args> void schedule(double t, link_attribute_change_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

template<typename... Args> void schedule(double t, wcmp_update_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

template<typename... Args> void schedule(double t, host_traffic_migration_func func, Args... args) {
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
        SWARM_INFO("All flows have been loaded, switching to simulation time reports");
        Simulator::Schedule(Simulator::Now(), reportTimeProgress, param_end);
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

void bindScenarioFunctions(scenario_functions<ClosTopology, FlowScheduler> *funcs) {
    funcs->link_down_func = disableLink;
    funcs->link_up_func = enableLink;
    funcs->set_bw_func = changeBandwidth;
    funcs->set_delay_func = changeDelay;
    funcs->set_wcmp_func = updateWcmp;
    funcs->link_loss_func = setLossRate;
    funcs->migrate_func = migrateTraffic;
}

void parseCmd(int argc, char* argv[], topolgoy_descriptor *topo_params) {
    CommandLine cmd(__FILE__);
    // Clos Topology parameters
    cmd.AddValue("numPods", "Number of Pods", topo_params->numPods);
    cmd.AddValue("switchRadix", "Switch radix", topo_params->switchRadix);
    cmd.AddValue("numServers", "Number of servers per edge switch", topo_params->numServers);

    // Link parameters
    cmd.AddValue("linkRate", "Link data rate in Gbps", topo_params->linkRate);
    cmd.AddValue("linkDelay", "Link delay in microseconds", topo_params->linkDelay);

    // Routing options
    cmd.AddValue("podBackup", "Enable backup routes in a pod", topo_params->enableEdgeBounceBackup);
    cmd.AddValue("plainEcmp", "Do normal ECMP", param_plain_ecmp);
    cmd.AddValue("cache", "Use a simple LRU cache for hash lookups", param_use_cache);

    // Inputs
    cmd.AddValue("scenario", "Path of the scenario file", param_scneario_file_path);
    cmd.AddValue("flow", "Path of the flow file", param_flow_file_path);

    // Simulation options
    cmd.AddValue("monitor", "Install FlowMonitor on the network", param_monitor);
    cmd.AddValue("scream", "Instruct all servers to scream at a given rate for the whole simulation", param_screamRate);
    cmd.AddValue("micro", "Set time resolution to micro-seconds", param_micro);
    cmd.AddValue("tcp", "Set the TCP variant to use", param_tcp_variant);
    cmd.AddValue("out", "Flow Monitor output prefix name", FLOW_FILE_PREFIX);
    
    #if MPI_ENABLED
    cmd.AddValue("mpi", "Enable MPI", topo_params->mpi);
    cmd.AddValue("superMpi", "Enable super MPI", param_super_mpi);
    #endif /* MPI_ENABLED */

    #if NETANIM_ENABLED
    cmd.AddValue("vis", "Create NetAnim input", topo_params->animate);
    #endif /* NETANIM_ENABLED */

    cmd.AddValue("end", "When to end simulation", param_end);

    // Logging options
    cmd.AddValue("verbose", "Enable debug log outputs", param_verbose);

    cmd.Parse(argc, argv);

    #if MPI_ENABLED
    if (param_super_mpi)
        topo_params->mpi = true;
    #endif
}

uint32_t setupSwarmSimulator(int argc, char* argv[], topology_descriptor_t *topo_params) {
    #if MPI_ENABLED
    if (topo_params->mpi) {
        GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::DistributedSimulatorImpl"));
        MpiInterface::Enable(&argc, &argv);

        systemId = MpiInterface::GetSystemId();
        systemCount = MpiInterface::GetSize();

        SWARM_INFO("MPI enabled, with total system count of " << systemCount);
        if (param_super_mpi)
            SWARM_WARN("Super-MPI has been enabled, the simulation will use a lot of memory!");
    }
    #endif /* MPI_ENABLED */

    if (param_micro)
        Time::SetResolution(Time::US);
    else
        Time::SetResolution(Time::NS);

    if (param_verbose)
        SWARM_SET_LOG_LEVEL(DEBG);

    logDescriptors(topo_params);

    if (!param_monitor)
        SWARM_WARN("Flow monitoring is DISABLED");
    
    SWARM_INFO("Creating SWARM topology");
    uint32_t totalNumberOfServers = topo_params->numPods * topo_params->switchRadix * topo_params->numServers / 2;
    SWARM_INFO("Total number of servers " << totalNumberOfServers);

    return totalNumberOfServers;
}

void setupClosTopology(ClosTopology *nodes) {
    // Create nodes and links
    nodes->createTopology();
    nodes->createLinks();

    // Assign IP addresses and create fabric interfaces
    nodes->assignServerIps();
    nodes->createFabricInterfaces();

    // Do ECMP
    nodes->setupServerRouting();
    nodes->setupCoreRouting();
    nodes->doEcmp();

    // Enable backup paths on aggregations
    if (nodes->params.enableEdgeBounceBackup) {
        SWARM_INFO("Enabling intra-pod backup routes");
        nodes->enableAggregateBackupPaths();
    }

    nodes->printSystemIds();
}

template<typename T> 
void setupMonitoringAndBeingExperiment(
    ClosTopology *nodes, 
    uint32_t totalNumberOfServers,
    string flow_output_file_name
    ) {

    // Setup flow monitor
    T flowMonitorHelper;
    if (param_monitor) {
        Ptr<Node> ptr;
        SWARM_INFO("Installing Flow Monitor on all local servers");
        for (uint32_t i = 0; i < totalNumberOfServers; i++) {
            if ((ptr = nodes->getLocalHost(i))) {
                flowMonitorHelper.Install(ptr);
            }
        }
    }

    // Get the flow file
    FlowScheduler *flowScheduler = nullptr;
    if (param_flow_file_path.length()) {
        SWARM_INFO("Scheduling flows on network from " << param_flow_file_path);

        // Bind the flow dispatcher function
        host_flow_dispatcher_function = [nodes](host_flow *flow) {
            return closHostFlowDispatcher(flow, nodes);
        };

        // Install packet sinks on each server
        nodes->installTcpPacketSinks();

        // Create the flow scheduler
        flowScheduler = new FlowScheduler(param_flow_file_path, host_flow_dispatcher_function);
    }

    // Bind scenario functions and get the file
    scenario_functions<ClosTopology, FlowScheduler> funcs;
    if (param_scneario_file_path.length()) {
        SWARM_INFO("Using scenarios specified in " << param_scneario_file_path);

        bindScenarioFunctions(&funcs);
        if (
            parseSecnarioScript<ClosTopology, FlowScheduler>(
                param_scneario_file_path, nodes, flowScheduler, &funcs
        ))
        {
            NS_ABORT_MSG("Scenario file could not be parsed, aborting");
        }
    }
    
    // Do constant all-to-all stream if needed
    if (param_screamRate.length()) {
        SWARM_INFO("Doing all-to-all TCP scream with a rate of " << param_screamRate);
        nodes->doAllToAllTcp(totalNumberOfServers, param_screamRate);
    }

    SWARM_INFO("Starting applications");
    if (flowScheduler)
        flowScheduler->begin();

    // nodes->echoBetweenHosts(0, totalNumberOfServers-1);

    nodes->startApplications(APPLICATION_START_TIME, param_end);

    DoReportProgress(param_end, flowScheduler);
    
    auto t_start = std::chrono::system_clock::now();

    Simulator::Stop(Seconds(param_end + QUIET_INTERVAL_LENGTH));
    Simulator::Run();
    Simulator::Destroy();

    auto t_end = std::chrono::system_clock::now();

    std::chrono::duration<float> took = t_end - t_start;
    SWARM_INFO("Run finished! Took " << (std::chrono::duration_cast<std::chrono::milliseconds>(took).count()) / 1000.0 << " s");

    // Serialize the results
    if (param_monitor) {
        SWARM_INFO("Serializing FCT information into prefix " + flow_output_file_name);
        flowMonitorHelper.SerializeToXmlFile(flow_output_file_name, false, false);
    }
}

int main(int argc, char *argv[]) {
    topolgoy_descriptor topo_params;

    SWARM_SET_LOG_LEVEL(INFO);

    // ns3::RngSeedManager::SetSeed(123456789);

    /**
     * TODO: Why in the holly names on earth, do we even need to do this ???????
     *       Where does MPI add this thing ??????
    */
    NS_OBJECT_ENSURE_REGISTERED(SocketIpv6TclassTag);

    parseCmd(argc, argv, &topo_params);

    // First, do global configurations
    doGlobalConfigs();

    uint32_t totalNumberOfServers = setupSwarmSimulator(
        argc, argv, &topo_params
    );

    // First, bind our level mapping function for WCMP
    wcmp_level_mapper = [topo_params](Ipv4Address dest) {
        return torLevelMapper(dest, &topo_params);
    };

    // Create the topology
    ClosTopology nodes = ClosTopology(topo_params);
    setupClosTopology(&nodes);

    // Setup FlowMonitor and begin the experiment
    #if MPI_ENABLED
    MpiFlowMonitorHelper::SetSystemId(systemId);

    if (topo_params.mpi) {
        setupMonitoringAndBeingExperiment<MpiFlowMonitorHelper>(
            &nodes, 
            totalNumberOfServers,
            FLOW_FILE_PREFIX
        );

        MpiInterface::Disable();
    }
    else {
        setupMonitoringAndBeingExperiment<FlowMonitorHelper>(
            &nodes, 
            totalNumberOfServers,
            FLOW_FILE_OUTPUT
        );
    }
    #else
        setupMonitoringAndBeingExperiment<FlowMonitorHelper>(
            &nodes, 
            totalNumberOfServers,
            FLOW_FILE_OUTPUT
        );
    #endif

    return 0;
}
