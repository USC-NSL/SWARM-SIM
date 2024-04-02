#include "swarm.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-helper.h"


using namespace ns3;
NS_LOG_COMPONENT_DEFINE(COMPONENT_NAME);

void logDescriptors(topolgoy_descriptor *topo_params) {
    NS_LOG_INFO("[INFO] Building FatTree with the following params:");

    char buf[128];
    int n;

    n = snprintf(buf, 128, "\tLink Rate = %d Gbps", topo_params->linkRate);
    buf[n] = '\0';
    NS_LOG_INFO(buf);
    
    n = snprintf(buf, 128, "\tLink Delay = %d us", topo_params->linkDelay);
    buf[n] = '\0';
    NS_LOG_INFO(buf);
    
    n = snprintf(buf, 128, "\tSwitch Radix = %d", topo_params->switchRadix);
    buf[n] = '\0';
    NS_LOG_INFO(buf);
    
    n = snprintf(buf, 128, "\tNumber of Servers Per Edge = %d", topo_params->numServers);
    buf[n] = '\0';
    NS_LOG_INFO(buf);
    
    n = snprintf(buf, 128, "\tNumber of Pods = %d", topo_params->numPods);
    buf[n] = '\0';
    NS_LOG_INFO(buf);

    if (topo_params->animate) {
        NS_LOG_INFO("[INFO] Will output NetAnim XML file to " + ANIM_FILE_OUTPUT);
    }

    if (topo_params->switchRadix / 2 < topo_params->numServers) {
        NS_LOG_WARN("[WARN] Number of servers exceeds half the radix. This topology is oversubscribed!");
    }

    NS_ASSERT(topo_params->switchRadix >= topo_params->numPods);
}

ClosTopology :: ClosTopology(const topology_descriptor_t m_params) {
    params = m_params;
}

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
    */

    // We separate the core to an ODD and EVEN part for easier linking
    this->coreSwitchesEven.Create(this->params.switchRadix / 2);
    this->coreSwitchesOdd.Create(this->params.switchRadix / 2);
    
    // We separate aggregate and edges for each pod for easier linking
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    for (uint32_t i = 0; i < this->params.numPods; i++) {
        this->aggSwitches.push_back(NodeContainer(numAggAndEdgeeSwitchesPerPod));
        this->edgeSwitches.push_back(NodeContainer(numAggAndEdgeeSwitchesPerPod));
    }

    // Create servers
    this->createServers();

    // Install the layer 3 stack
    InternetStackHelper internet;
    internet.Install(coreSwitchesEven);
    internet.Install(coreSwitchesOdd);
    for (uint32_t i = 0; i < this->params.numPods; i++) {
        internet.Install(this->aggSwitches[i]);
        internet.Install(this->edgeSwitches[i]);
        for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
            internet.Install(this->servers[i * numAggAndEdgeeSwitchesPerPod + j]);
        }
    }
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
    if (this->params.animate) {
        this->setNodeCoordinates();
    }
}

void ClosTopology :: createServers() {
    // There will be `numServers * 2.switchRadix` servers in total
    // We key them with (pod_num, edge_switch_index) --> NodeContainer
    
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;

    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            NodeContainer edgeServers(this->params.numServers);
            this->servers[pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx] = edgeServers;
            for (uint32_t i = 0; i < this->params.numServers; i++) {
                this->serverApplications.push_back(ApplicationContainer());
            }
        }
    }
}

void ClosTopology :: connectServers() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;

    // All links have the same spec
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

void ClosTopology :: createFabricInterfaces() {
    /**
     * Create interfaces for the core, aggregate and edge switches,
     * without assigning any IP address to them.
    */

    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    uint32_t ifIndexSrc, ifIndexDst;

    // First, Edge to Agg
    NetDeviceContainer currentDevices;
    Ptr<Ipv4> ipv4Src, ipv4Dst;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            for (uint32_t j = 0; j < numAggAndEdgeeSwitchesPerPod; j++) {
                currentDevices = this->edgeToAggLinks[std::make_tuple(
                    pod_num * numAggAndEdgeeSwitchesPerPod + i,
                    pod_num * numAggAndEdgeeSwitchesPerPod + j
                )];
                
                // Now, create IPv4 instances for each node or fetch it
                ipv4Src = currentDevices.Get(0)->GetNode()->GetObject<Ipv4>();
                ipv4Dst = currentDevices.Get(1)->GetNode()->GetObject<Ipv4>();

                // Create the interfaces
                ifIndexSrc = ipv4Src->AddInterface(currentDevices.Get(0));
                ifIndexDst = ipv4Dst->AddInterface(currentDevices.Get(1));

                // Set the interfaces up
                ipv4Src->SetUp(ifIndexSrc);
                ipv4Dst->SetUp(ifIndexDst);

                // Since all routes are static, we can make all interfaces look like loopbacks
                ipv4Src->AddAddress(ifIndexSrc, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("255.0.0.0")));
                ipv4Dst->AddAddress(ifIndexDst, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("255.0.0.0")));
            }
        }
    }

    // Now Core
    for (auto const& map_elem : this->aggToCoreLinks) {
        currentDevices = map_elem.second;

        ipv4Src = currentDevices.Get(0)->GetNode()->GetObject<Ipv4>();
        ipv4Dst = currentDevices.Get(1)->GetNode()->GetObject<Ipv4>();
        ifIndexSrc = ipv4Src->AddInterface(currentDevices.Get(0));
        ifIndexDst = ipv4Dst->AddInterface(currentDevices.Get(1));
        ipv4Src->SetUp(ifIndexSrc);
        ipv4Dst->SetUp(ifIndexDst);
        ipv4Src->AddAddress(ifIndexSrc, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("255.0.0.0")));
        ipv4Dst->AddAddress(ifIndexDst, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("255.0.0.0")));
    }

    // Edge to Servers (skip the Servers, they already have it)
    Ptr<NetDevice>  device;
    
    uint32_t switchIndex, serverIndex;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t i = 0; i < numAggAndEdgeeSwitchesPerPod; i++) {
            switchIndex = pod_num * numAggAndEdgeeSwitchesPerPod + i;
            for (uint32_t j = 0; j < this->params.numServers; j++) {
                serverIndex = switchIndex * this->params.numServers + j;
                device = this->serverToEdgeLinks[std::make_tuple(switchIndex, serverIndex)].Get(1);
                ipv4Src = device->GetNode()->GetObject<Ipv4>();
                ifIndexSrc = ipv4Src->AddInterface(device);
                ipv4Src->SetUp(ifIndexSrc);
                ipv4Src->AddAddress(ifIndexSrc, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("255.0.0.0")));
            }
        }
    }
}

void ClosTopology :: assignServerIps() {
    /**
     * This only assigns IPs to servers.
     * It makes a /24 LAN for each server under a ToR.
     * This is only useful if WCMP routing is used.
    */

    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4AddressHelper edgeLanAddresses;
    char buf[16];

    uint32_t switch_idx;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            switch_idx = pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx;
            snprintf(buf, 16, "10.%hu.%hu.0", pod_num, edge_idx);
            edgeLanAddresses.SetBase(Ipv4Address(buf), edgeLanAddressMask);
            this->serverInterfaces[switch_idx] = edgeLanAddresses.Assign(this->serverDevices[switch_idx]);
        }
    }
}

void ClosTopology :: assignIpsNaive() {
    /**
     * This just makes a little LAN for each link in the network
     * Results in an unnecessarily big routing table, but that may not matter in 
     * this simulation.
     * 
     * UPDATE: It does matter! It just does not work when the radix is large
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

void ClosTopology :: echoBetweenHosts(uint32_t client_host, uint32_t server_host, double interval) {
    // NS_LOG_INFO("[INFO] Doing echo between " << this->getServerAddress(server_host) << " and " << this->getServerAddress(client_host));
    UdpEchoServerHelper echoServer(UDP_DISCARD_PORT);
    UdpEchoClientHelper echoClient(this->getServerAddress(server_host), UDP_DISCARD_PORT);

    echoClient.SetAttribute("MaxPackets", UintegerValue(100));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(interval)));
    echoClient.SetAttribute("PacketSize", UintegerValue(64));
    
    this->serverApplications[server_host].Add(echoServer.Install(this->getHost(server_host)));
    this->serverApplications[client_host].Add(echoClient.Install(this->getHost(client_host)));
}

void ClosTopology :: unidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate) {
    UdpEchoServerHelper echoServer(UDP_DISCARD_PORT);
    NS_LOG_INFO("[INFO] Starting unidirectional flow to " << this->getServerAddress(server_host));
    OnOffHelper onOffClient("ns3::UdpSocketFactory", InetSocketAddress(this->getServerAddress(server_host), UDP_DISCARD_PORT));

    onOffClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onOffClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onOffClient.SetAttribute("DataRate", StringValue(rate));
    onOffClient.SetAttribute("PacketSize", UintegerValue(UDP_PACKET_SIZE_SMALL));
    
    this->serverApplications[server_host].Add(echoServer.Install(this->getHost(server_host)));
    this->serverApplications[client_host].Add(onOffClient.Install(this->getHost(client_host)));
}

void changeBandwidth(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr) {
    ns3::NetDeviceContainer link = topology->getLink(src_level, src_idx, dst_level, dst_idx);
    link.Get(0)->SetAttribute("DataRate", ns3::StringValue(dataRateStr));
    link.Get(1)->SetAttribute("DataRate", ns3::StringValue(dataRateStr));
}

void changeDelay(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr) {
    ns3::NetDeviceContainer link = topology->getLink(src_level, src_idx, dst_level, dst_idx);
    link.Get(0)->GetChannel()->SetAttribute("Delay", ns3::StringValue(delayStr));
}

void disableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    ns3::NetDeviceContainer link = topology->getLink(src_level, src_idx, dst_level, dst_idx);
    ns3::Ptr<ns3::Ipv4> if0 = link.Get(0)->GetNode()->GetObject<ns3::Ipv4>();
    ns3::Ptr<ns3::Ipv4> if1 = link.Get(1)->GetNode()->GetObject<ns3::Ipv4>();
    if0->SetDown(link.Get(0)->GetIfIndex());
    if1->SetDown(link.Get(1)->GetIfIndex());
}

void enableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
    ns3::NetDeviceContainer link = topology->getLink(src_level, src_idx, dst_level, dst_idx);
    ns3::Ptr<ns3::Ipv4> if0 = link.Get(0)->GetNode()->GetObject<ns3::Ipv4>();
    ns3::Ptr<ns3::Ipv4> if1 = link.Get(1)->GetNode()->GetObject<ns3::Ipv4>();
    if0->SetUp(link.Get(0)->GetIfIndex());
    if1->SetUp(link.Get(1)->GetIfIndex());
}

template<typename... Args> void schedule(double t, link_state_change_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

template<typename... Args> void schedule(double t, link_attribute_change_func func, Args... args) {
    ns3::Simulator::Schedule(ns3::Seconds(t), func, args...);
}

void ClosTopology :: doStaticRouteOnEdges() {
    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4StaticRoutingHelper staticHelper;

    char buf[16];
    uint32_t switch_idx;
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            switch_idx = pod_num * numAggAndEdgeeSwitchesPerPod + edge_idx;
            for (uint32_t i = 1; i <= this->params.numServers; i++) {
                // The IP address of i'th server in pod `pod_num` under the edge_idx`th ToR is 
                // `10.pod_num.edge_idx.i` with mask /24
                snprintf(buf, 16, "10.%hu.%hu.%hu", pod_num, edge_idx, i);
                staticHelper.GetStaticRouting(this->getEdge(switch_idx)->GetObject<Ipv4>())->AddHostRouteTo(
                    Ipv4Address(buf), Ipv4Address(buf), i + this->params.switchRadix/2
                );
                staticHelper.GetStaticRouting(this->getHost(switch_idx, i-1)->GetObject<Ipv4>())->AddNetworkRouteTo(
                    Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), 1
                );
            }
        }
    }
}

void ClosTopology :: installWcmpStack() {
    /**
     * This installs the WCMP stack on all of the fabric nodes in the
     * topology. 
     * By default, they are configured to do ECMP.
    */

    WcmpStaticRoutingHelper wcmpHelper;
    Ipv4ListRoutingHelper listHelper;
    InternetStackHelper stackHelper;

    // Add the WCMP stack
    listHelper.Add(wcmpHelper, WCMP_LIST_ROUTING_PRIORITY);
    stackHelper.SetRoutingHelper(listHelper);

    stackHelper.Install(coreSwitchesEven);
    stackHelper.Install(coreSwitchesOdd);
    for (auto & element : this->aggSwitches)
        stackHelper.Install(element);
    for (auto & element : this->edgeSwitches)
        stackHelper.Install(element);
}

void ClosTopology :: doEcmp() {
    /**
     * Simple ECMP implemenation.
     * The weights can be modified, so it kind of doubles as WCMP, but initially,
     * it only performs ECMP.
    */

    /**
     * (Assume switch radix is `r` and number of servers under each edge is `n` and number of pods is `p`)
     * With how we assign IP addresses or interfaces, the interface indices would be like the following:
     *  - SERVERS: Only a single interface of index 1, which always has an IP address
     *  - EDGES: Indices 1 ... r/2 will go towards the aggregation. Indices r/2+1 ... r/2+n will go to servers.
     *  - AGGREGATES: Indices 1 ... r/2 will go towards the edges. Indices r/2+1 ... r will go the core.
     *  - CORES: Indices 1 ... p will go towards the aggregation.
     * 
     * We must make sure that the following always holds:
     *  - Traffic between servers under the same ToR, stays under that ToR
     *  - Traffic betwenn servers in the same pod, stays in that pod
     * 
     * `doStaticRoutingOnEdges` will handle the first one, the rest we handle right here.
     * 
     * This is also where we add the WCMP routing stack.
    */

    uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
    Ipv4StaticRoutingHelper staticHelper;
    
    char buf[16];

    // The edges just need a default route towards the aggregators
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
            for (uint32_t if_index = 1; if_index <= numAggAndEdgeeSwitchesPerPod; if_index++) {
                staticHelper.GetStaticRouting(this->getEdge(pod_num, edge_idx)->GetObject<Ipv4>())->AddNetworkRouteTo(
                    Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), if_index
                );
            }
        }
    }

    // Aggregates just route towards any interface facing their own pod, else give it to any core
    for (uint32_t pod_num = 0; pod_num < this->params.numPods; pod_num++) {
        for (uint32_t agg_idx = 0; agg_idx < numAggAndEdgeeSwitchesPerPod; agg_idx++) {
            // To edges
            for (uint32_t edge_idx = 0; edge_idx < numAggAndEdgeeSwitchesPerPod; edge_idx++) {
                snprintf(buf, 16, "10.%hu.%hu.0", pod_num, edge_idx);
                staticHelper.GetStaticRouting(this->getAggregate(pod_num, agg_idx)->GetObject<Ipv4>())->AddNetworkRouteTo(
                    Ipv4Address(buf), Ipv4Mask("255.255.255.0"), edge_idx + 1
                );
            }

            // To cores
            for (uint32_t idx = 0; idx < numAggAndEdgeeSwitchesPerPod; idx++) {
                staticHelper.GetStaticRouting(this->getAggregate(pod_num, agg_idx)->GetObject<Ipv4>())->AddNetworkRouteTo(
                Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), idx + numAggAndEdgeeSwitchesPerPod + 1
                );
            }
        }
    }

    // Cores will route to the correct pod
    for (uint32_t core_idx = 0; core_idx < this->params.switchRadix; core_idx++) {
        for (uint32_t pod_num=0; pod_num < this->params.numPods; pod_num++) {
            snprintf(buf, 16, "10.%d.0.0", pod_num);
            staticHelper.GetStaticRouting(this->getCore(core_idx)->GetObject<Ipv4>())->AddNetworkRouteTo(
                Ipv4Address(buf), Ipv4Mask("255.255.0.0"), pod_num+1
            );
        }
    }
}

int main(int argc, char *argv[]) {
    Time::SetResolution(Time::US);

    // Naive ECMP
    Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting", BooleanValue(true));
    Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));

    topolgoy_descriptor topo_params;
    // bool verbose = false;

    ns3::LogComponentEnable(COMPONENT_NAME, LOG_LEVEL_INFO);

    CommandLine cmd(__FILE__);
    cmd.AddValue("linkRate", "Link data rate in Gbps", topo_params.linkRate);
    cmd.AddValue("linkDelay", "Link delay in microseconds", topo_params.linkDelay);
    cmd.AddValue("switchRadix", "Switch radix", topo_params.switchRadix);
    cmd.AddValue("numServers", "Number of servers per edge switch", topo_params.numServers);
    cmd.AddValue("numPods", "Number of Pods", topo_params.numPods);
    cmd.AddValue("vis", "Create NetAnim input", topo_params.animate);
    cmd.Parse(argc, argv);

    logDescriptors(&topo_params);
    
    // Create the topology
    ClosTopology nodes = ClosTopology(topo_params);
    nodes.createTopology();
    nodes.createLinks();

    // Install the routing stack
    nodes.installWcmpStack();

    // Assign IP addresses
    // nodes.assignIps();
    // nodes.assignIpsNaive();
    nodes.assignServerIps();
    nodes.createFabricInterfaces();
    nodes.doStaticRouteOnEdges();

    // Do God Routing
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    nodes.doEcmp();

    // Echo test ...
    // ns3::LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    // ns3::LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    // ns3::LogComponentEnable("Ipv4StaticRouting", LOG_LEVEL_DEBUG);

    // nodes.echoBetweenHosts(0, 1);
    // nodes.echoBetweenHosts(1, 5);
    nodes.unidirectionalCbrBetweenHosts(0, 4, "200kbps");
    nodes.startApplications(1.0, 4);
    // schedule(1.5, disableLink, &nodes, EDGE, 0, AGGREGATE, 0);

    Ptr<OutputStreamWrapper> routingStream =
        Create<OutputStreamWrapper>("swarm.routes", std::ios::out);
    // Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(1.0), nodes.getHost(0), routingStream);
    // Ipv4RoutingHelper::PrintRoutingTableAt(Seconds(1.0), nodes.getHost(1), routingStream);
    Ipv4ListRoutingHelper::PrintRoutingTableAt(Seconds(1.0), nodes.getAggregate(3), routingStream);
    // PointToPointHelper p2p;
    // p2p.EnablePcapAll("swarm.pcap");

    Simulator::Stop(Seconds(5.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}


