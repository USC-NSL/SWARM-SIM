#include <iomanip>
#include <unistd.h>
#include <mpi.h>
#include "offline.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-flow-classifier.h"

using namespace ns3;


std::vector<double> throughputAnalysis(double loss_rate, uint32_t rtt) {
    std::vector<double> throughputs;

    if (systemId == 0)
        std::cout << "Evaluating LOSS = " << loss_rate << " and RTT = " << rtt << std::endl;
    usleep(500);

    for (uint32_t i = 0; i < NUMBER_OF_EXPERIMENT_REPEATS_LONG; i++) {
        if (!isCorrectIteration(i))
            continue;

        std::cout << "[" << systemId << "]" << "Iteration " << i << std::endl;
        // Create the topology
        Ptr<Node> h1 = CreateObject<Node>();
        Ptr<Node> h2 = CreateObject<Node>();
        Ptr<Node> s1 = CreateObject<Node>();
        Ptr<Node> s2 = CreateObject<Node>();

        PointToPointHelper p2p;
        // p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(DEFAULT_LINK_RATE) + "Gbps"));
        p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(DEFAULT_LINK_RATE) + "Mbps"));
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string(DELAY_A_B) + "us"));
        NodeContainer h1s1 = NodeContainer(h1);
        h1s1.Add(s1);
        NetDeviceContainer dh1s1 = p2p.Install(h1s1);
        
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string(DELAY_A_B) + "us"));
        NodeContainer s2h2 = NodeContainer(s2);
        s2h2.Add(h2);
        NetDeviceContainer ds2h2 = p2p.Install(s2h2);

        NS_ASSERT(rtt >= (2*DELAY_A_B +2* DELAY_A_B + 2));

        // Assign the delay for s1 and s2 link
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string((rtt - (4 * DELAY_A_B)) / 2) + "us"));
        NodeContainer s1s2 = NodeContainer(s1);
        s1s2.Add(s2);
        NetDeviceContainer ds1s2 = p2p.Install(s1s2);

        InternetStackHelper internet;
        internet.InstallAll();

        // Assign IPs
        Ipv4AddressHelper ipv4;
        ipv4.SetBase("10.0.0.0", "/24");
        ipv4.Assign(dh1s1); // 10.0.0.1 --- 10.0.0.2
        ipv4.SetBase("10.0.1.0", "/24");
        ipv4.Assign(ds2h2); // 10.0.1.1 --- 10.0.1.2
        ipv4.SetBase("10.0.2.0", "/24");
        ipv4.Assign(ds1s2); // 10.0.2.1 --- 10.0.2.2

        Ipv4Address src = Ipv4Address("10.0.0.1");
        Ipv4Address dst = Ipv4Address("10.0.1.2");

        // Error model
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetRate(loss_rate);
        em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
        ds1s2.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        ds1s2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        NS_ASSERT(em->IsEnabled());

        // Just use God routing
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();

        // FlowMonitor
        FlowMonitorHelper flowMonitor;
        flowMonitor.Install(h1);
        flowMonitor.Install(h2);

        // Install sink and bulk applications
        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
        ApplicationContainer sinkApplication = sink.Install(h2);
        BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
        bulk.SetAttribute("SendSize", UintegerValue(6000));
        ApplicationContainer bulkApplication = bulk.Install(h1);
        sinkApplication.Start(Seconds(0.05));
        bulkApplication.Start(Seconds(0.1));

        Simulator::Stop(MilliSeconds(RUNTIME + 100));
        Simulator::Run();
        Simulator::Destroy();

        // Get throughput
        bool found = false;
        Ptr<FlowMonitor> monitor = flowMonitor.GetMonitor();
        FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
        FlowMonitor::FlowProbeContainer probes = monitor->GetAllProbes();
        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());
        for (FlowMonitor::FlowStatsContainerCI stat = stats.begin(); stat != stats.end(); stat++) {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(stat->first);
            if (tuple.destinationAddress == dst && tuple.sourceAddress == src && tuple.destinationPort == TCP_DISCARD_PORT) {
                throughputs.push_back((static_cast<double>(stat->second.rxBytes) * 1000 / RUNTIME));
                found = true;
                break;
            }
        }
        NS_ASSERT(found);
    }

    return throughputs;
}

std::vector<uint32_t> rttAnalysis(double loss_rate, uint32_t rtt, uint32_t flowSize) {
    std::vector<uint32_t> rttCounts;

    if (systemId == 0)
        std::cout << "Evaluating LOSS = " << loss_rate << " and RTT = " << rtt << " and FlowSize = " << flowSize << std::endl;
    usleep(500);

    for (uint32_t i = 0; i < NUMBER_OF_EXPERIMENT_REPEATS_SHORT; i++) {
        if (!isCorrectIteration(i))
            continue;

        std::cout << "[" << systemId << "]" << "Iteration " << i << std::endl;
        // Create the topology
        Ptr<Node> h1 = CreateObject<Node>();
        Ptr<Node> h2 = CreateObject<Node>();
        Ptr<Node> s1 = CreateObject<Node>();
        Ptr<Node> s2 = CreateObject<Node>();

        PointToPointHelper p2p;
        // p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(DEFAULT_LINK_RATE) + "Gbps"));
        p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(DEFAULT_LINK_RATE) + "Mbps"));
        
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string(DELAY_A_B) + "us"));
        NodeContainer h1s1 = NodeContainer(h1);
        h1s1.Add(s1);
        NetDeviceContainer dh1s1 = p2p.Install(h1s1);
        
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string(DELAY_A_B) + "us"));
        NodeContainer s2h2 = NodeContainer(s2);
        s2h2.Add(h2);
        NetDeviceContainer ds2h2 = p2p.Install(s2h2);

        NS_ASSERT(rtt >= (4 * DELAY_A_B + 2));

        // Assign the delay for s1 and s2 link
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string((rtt - (4 * DELAY_A_B)) / 2) + "us"));
        NodeContainer s1s2 = NodeContainer(s1);
        s1s2.Add(s2);
        NetDeviceContainer ds1s2 = p2p.Install(s1s2);

        InternetStackHelper internet;
        internet.InstallAll();

        // Assign IPs
        Ipv4AddressHelper ipv4;
        ipv4.SetBase("10.0.0.0", "/24");
        ipv4.Assign(dh1s1); // 10.0.0.1 --- 10.0.0.2
        ipv4.SetBase("10.0.1.0", "/24");
        ipv4.Assign(ds2h2); // 10.0.1.1 --- 10.0.1.2
        ipv4.SetBase("10.0.2.0", "/24");
        ipv4.Assign(ds1s2); // 10.0.2.1 --- 10.0.2.2

        Ipv4Address src = Ipv4Address("10.0.0.1");
        Ipv4Address dst = Ipv4Address("10.0.1.2");

        // Error model
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetRate(loss_rate);
        em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
        ds1s2.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        ds1s2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        NS_ASSERT(em->IsEnabled());

        // Just use God routing
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();

        // FlowMonitor
        FlowMonitorHelper flowMonitor;
        flowMonitor.Install(h1);
        flowMonitor.Install(h2);

        // Install sink and bulk applications
        PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
        ApplicationContainer sinkApplication = sink.Install(h2);
        BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
        bulk.SetAttribute("SendSize", UintegerValue(6000));
        bulk.SetAttribute("MaxBytes", UintegerValue(flowSize));
        ApplicationContainer bulkApplication = bulk.Install(h1);
        sinkApplication.Start(Seconds(0.05));
        bulkApplication.Start(Seconds(0.1));

        Simulator::Stop(MilliSeconds(RUNTIME + 100));
        Simulator::Run();
        Simulator::Destroy();

        // Get throughput
        bool found = false;
        Ptr<FlowMonitor> monitor = flowMonitor.GetMonitor();
        FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
        FlowMonitor::FlowProbeContainer probes = monitor->GetAllProbes();
        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());
        for (FlowMonitor::FlowStatsContainerCI stat = stats.begin(); stat != stats.end(); stat++) {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(stat->first);
            if (tuple.destinationAddress == dst && tuple.sourceAddress == src && tuple.destinationPort == TCP_DISCARD_PORT) {
                rttCounts.push_back(
                    (stat->second.timeLastRxPacket.GetMicroSeconds() - stat->second.timeFirstTxPacket.GetMicroSeconds()) / rtt
                );
                if (!found)
                    found = true;
            }
        }
        NS_ASSERT(found);
    }

    return rttCounts;
}

std::vector<int> queueDelayAnalysis(uint32_t N, uint32_t M) {
    std::vector<int> delays;
    if (systemId == 0)
        std::cout << "Evaluating N = " << N << " and M = " << M << std::endl;
    usleep(500);

    for (uint32_t i = 0; i < NUMBER_OF_EXPERIMENT_REPEATS_SHORT; i++) {
        if (!isCorrectIteration(i))
            continue;

        uint32_t localPortStart = 1000;

        std::cout << "[" << systemId << "]" << "Iteration " << i << std::endl;
        // Create the topology
        Ptr<Node> h1 = CreateObject<Node>();
        Ptr<Node> h2 = CreateObject<Node>();
        Ptr<Node> h3 = CreateObject<Node>();
        Ptr<Node> h4 = CreateObject<Node>();
        Ptr<Node> h5 = CreateObject<Node>();
        Ptr<Node> s1 = CreateObject<Node>();
        Ptr<Node> s2 = CreateObject<Node>();

        PointToPointHelper p2p;
        p2p.SetChannelAttribute("Delay", StringValue(std::to_string(DEFAULT_LINK_DELAY) + "us"));
        // p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(DEFAULT_LINK_RATE) + "Gbps"));
        p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(DEFAULT_LINK_RATE) + "Mbps"));
        
        NodeContainer h1s1 = NodeContainer(h1);
        h1s1.Add(s1);
        NetDeviceContainer dh1s1 = p2p.Install(h1s1);
        NodeContainer h3s1 = NodeContainer(h3);
        h3s1.Add(s1);
        NetDeviceContainer dh3s1 = p2p.Install(h3s1);
        NodeContainer h4s1 = NodeContainer(h4);
        h4s1.Add(s1);
        NetDeviceContainer dh4s1 = p2p.Install(h4s1);
        NodeContainer s2h2 = NodeContainer(s2);
        s2h2.Add(h2);
        NetDeviceContainer ds2h2 = p2p.Install(s2h2);
        NodeContainer s2h5 = NodeContainer(s2);
        s2h5.Add(h5);
        NetDeviceContainer ds2h5 = p2p.Install(s2h5);
        NodeContainer s1s2 = NodeContainer(s1);
        s1s2.Add(s2);
        NetDeviceContainer ds1s2 = p2p.Install(s1s2);

        InternetStackHelper internet;
        internet.InstallAll();

        // Assign IPs
        Ipv4AddressHelper ipv4;
        ipv4.SetBase("10.0.0.0", "/24");
        ipv4.Assign(dh1s1); // 10.0.0.1 --- 10.0.0.2
        ipv4.SetBase("10.0.1.0", "/24");
        ipv4.Assign(ds2h2); // 10.0.1.1 --- 10.0.1.2
        ipv4.SetBase("10.0.2.0", "/24");
        ipv4.Assign(dh3s1); // 10.0.2.1 --- 10.0.2.2
        ipv4.SetBase("10.0.3.0", "/24");
        ipv4.Assign(dh4s1); // 10.0.3.1 --- 10.0.3.2
        ipv4.SetBase("10.0.4.0", "/24");
        ipv4.Assign(ds2h5); // 10.0.4.1 --- 10.0.4.2
        ipv4.SetBase("10.0.5.0", "/24");
        ipv4.Assign(ds1s2); // 10.0.5.1 --- 10.0.5.2

        Ipv4Address src = Ipv4Address("10.0.0.1");
        Ipv4Address dst = Ipv4Address("10.0.1.2");

        // FlowMonitor
        FlowMonitorHelper flowMonitor;
        flowMonitor.Install(h1);
        flowMonitor.Install(h2);

        // Just use God routing
        Ipv4GlobalRoutingHelper globalRouter;
        globalRouter.PopulateRoutingTables();

        // Install sink and bulk applications
        PacketSinkHelper sinkH2("ns3::TcpSocketFactory", InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
        PacketSinkHelper sinkH3("ns3::TcpSocketFactory", InetSocketAddress("10.0.2.1", TCP_DISCARD_PORT));
        PacketSinkHelper sinkH5("ns3::TcpSocketFactory", InetSocketAddress("10.0.4.2", TCP_DISCARD_PORT));
        ApplicationContainer sinkApplicationH2 = sinkH2.Install(h2);
        ApplicationContainer sinkApplicationH3 = sinkH3.Install(h3);
        ApplicationContainer sinkApplicationH5 = sinkH5.Install(h5);

        SingleFlowHelper shortHelper("ns3::TcpSocketFactory", InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
        shortHelper.SetAttribute("PacketSize", UintegerValue(DEFAULT_MSS));
        shortHelper.SetAttribute("FlowSize", UintegerValue(VERY_SHORT_FLOW_SIZE));
        ApplicationContainer shortApplication = shortHelper.Install(h1);
        
        ApplicationContainer bulkMContainer, bulkNContainer;

        BulkSendHelper bulkM("ns3::TcpSocketFactory", InetSocketAddress("10.0.2.1", TCP_DISCARD_PORT));
        for (uint32_t i = 0; i < M; i++) {
            bulkM.SetAttribute("SendSize", UintegerValue(6000));
            bulkM.SetAttribute("Local", AddressValue(InetSocketAddress("10.0.3.1", localPortStart)));
            bulkMContainer.Add(bulkM.Install(h4));
            ++localPortStart;
        }

        BulkSendHelper bulkN("ns3::TcpSocketFactory", InetSocketAddress("10.0.4.2", TCP_DISCARD_PORT));
        for (uint32_t i = 0; i < N; i++) {
            bulkN.SetAttribute("SendSize", UintegerValue(6000));
            bulkN.SetAttribute("Local", AddressValue(InetSocketAddress("10.0.3.1", localPortStart)));
            bulkNContainer.Add(bulkN.Install(h4));
            ++localPortStart;
        }

        sinkApplicationH2.Start(Seconds(0.05));
        sinkApplicationH3.Start(Seconds(0.05));
        sinkApplicationH5.Start(Seconds(0.05));
        bulkMContainer.Start(Seconds(0.1));
        bulkNContainer.Start(Seconds(0.1));
        shortApplication.Start(MilliSeconds(BIG_FLOW_STEADY_TIME));

        Ptr<SingleFlowApplication> shortFlowApplication = DynamicCast<SingleFlowApplication>(shortApplication.Get(0));
        shortFlowApplication->m_reportDone = true;

        for (uint32_t i = (BIG_FLOW_STEADY_TIME / CHECK_SHORT_COMPLETTION_EACH); i  < (RUNTIME / CHECK_SHORT_COMPLETTION_EACH); i++)
            Simulator::Schedule(MilliSeconds(BIG_FLOW_STEADY_TIME + i * CHECK_SHORT_COMPLETTION_EACH), checkShortIsDone, shortFlowApplication);

        Simulator::Stop(MilliSeconds(RUNTIME + 100));
        Simulator::Run();
        Simulator::Destroy();

        // Get throughput
        bool found = false;
        Ptr<FlowMonitor> monitor = flowMonitor.GetMonitor();
        FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
        FlowMonitor::FlowProbeContainer probes = monitor->GetAllProbes();
        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());
        for (FlowMonitor::FlowStatsContainerCI stat = stats.begin(); stat != stats.end(); stat++) {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(stat->first);
            if (tuple.destinationAddress == dst && tuple.sourceAddress == src && tuple.destinationPort == TCP_DISCARD_PORT) {
                if (stat->second.timeLastRxPacket.GetMicroSeconds() == 0) 
                    delays.push_back(-1);
                else
                    delays.push_back(
                        ((int)(stat->second.timeLastRxPacket.GetMicroSeconds() - stat->second.timeFirstTxPacket.GetMicroSeconds())) - (6 * DEFAULT_LINK_DELAY)
                    );
                if (!found)
                    found = true;
            }
        }
        NS_ASSERT(found);
    }

    return delays;
}

void doTpTest() {
    /********************************
     *     Throughput Analysis
     ********************************/

    std::map<std::tuple<double, uint32_t>, std::vector<double>> throughputs;
    std::ofstream output;

    std::cout << "Throuhgput analysis ..." << std::endl;
    output.open("throughputs-" + std::to_string(systemId) + ".csv");
    output << "LOSS_RATE,RTT,";
    for (uint32_t i = 1; i < NUMBER_OF_EXPERIMENT_REPEATS_LONG; i++)
        output << i << ",";
    output << NUMBER_OF_EXPERIMENT_REPEATS_LONG << '\n';

    for (const auto & input_packet_drop: input_packet_drops)
        for (const auto & input_rtt: input_rtts)
            throughputs[std::make_tuple(input_packet_drop, input_rtt)] = throughputAnalysis(
                input_packet_drop, input_rtt
            );

    for (const auto & it: throughputs) {
        output << std::setprecision(6) << std::get<0>(it.first) << ',' << std::get<1>(it.first) << ',';
        for (uint32_t i = 0; i < NUMBER_OF_EXPERIMENT_REPEATS_LONG; i++) {
            output << it.second[i];
            if (i != (NUMBER_OF_EXPERIMENT_REPEATS_LONG-1))
                output << ',';
        }
        output << '\n';
    }
    output.close();

}

void doRttTest() {
    /********************************
     *        RTT Analysis
     ********************************/

    std::map<std::tuple<double, uint32_t, uint32_t>, std::vector<uint32_t>> rttCounts;
    std::ofstream output;

    std::cout << "RTT count analysis ..." << std::endl;
    output.open("rtts-" + std::to_string(systemId) + ".csv");
    output << "LOSS_RATE,RTT,FLOW_SIZE,";
    for (uint32_t i = 1; i < NUMBER_OF_EXPERIMENT_REPEATS_SHORT; i++)
        output << i << ",";
    output << NUMBER_OF_EXPERIMENT_REPEATS_SHORT << '\n';

    for (const auto & input_packet_drop: input_packet_drops)
        for (const auto & input_rtt: input_rtts)
            for (const auto & input_flow_size: input_flow_sizes)
                rttCounts[std::make_tuple(input_packet_drop, input_rtt, input_flow_size)] = rttAnalysis(
                    input_packet_drop, input_rtt, input_flow_size
                );

    for (const auto & it: rttCounts) {
        output << std::setprecision(6) << std::get<0>(it.first) << ',' << std::get<1>(it.first) << ',' << std::get<2>(it.first) << ',';
        for (uint32_t i = 0; i < NUMBER_OF_EXPERIMENT_REPEATS_LONG; i++) {
            output << it.second[i];
            if (i != (NUMBER_OF_EXPERIMENT_REPEATS_LONG-1))
                output << ',';
        }
        output << '\n';
    }
    output.close();
}

void doDelayTest() {
    /********************************
     *     Queue Delay Analysis
     ********************************/

    std::map<std::tuple<uint32_t, double>, std::vector<int>> queueDelays;    
    std::ofstream output;

    output.open("delays-" + std::to_string(systemId) + ".csv");
    output << "N,u,";
    for (uint32_t i = 1; i < NUMBER_OF_EXPERIMENT_REPEATS_SHORT; i++)
        output << i << ",";
    output << NUMBER_OF_EXPERIMENT_REPEATS_SHORT << '\n';

    std::cout << "Queue delay analysis ..." << std::endl;
    for (const auto & u: input_utilizations)
        for (uint32_t i = 0; i < NUM_N; i++) {
            uint32_t n = ((N_HIGH - N_LOW) / NUM_N * i + N_LOW);
            uint32_t m = getMFromN(n, u);

            queueDelays[std::make_tuple(n, u)] = queueDelayAnalysis(
                n, m
            );
        }

    for (const auto & it: queueDelays) {
        output << std::get<0>(it.first) << ',' << std::get<1>(it.first) << ',';
        for (uint32_t i = 0; i < NUMBER_OF_EXPERIMENT_REPEATS_LONG; i++) {
            output << it.second[i];
            if (i != (NUMBER_OF_EXPERIMENT_REPEATS_LONG-1))
                output << ',';
        }
        output << '\n';
    }
    output.close();
}


int main(int argc, char *argv[]) {
    Time::SetResolution(Time::NS);

    CommandLine cmd(__FILE__);

    bool param_do_tp = false;
    bool param_do_rtt = false;
    bool param_do_delay = false;

    cmd.AddValue("tp", "Do throughput test", param_do_tp);
    cmd.AddValue("rtt", "Do RTT count test", param_do_rtt);
    cmd.AddValue("delay", "Do queue delay test", param_do_delay);

    MPI_Init(NULL, NULL);
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    systemCount = (uint32_t) world_size;
    systemId = (uint32_t) world_rank;

    if (systemId == 0)
        std::cout << "Running on " << systemCount << " proceses"  << std::endl;
    usleep(500);

    // Finalize the MPI environment.
    MPI_Finalize();

    cmd.Parse(argc, argv);

    if (param_do_tp)
        doTpTest();

    if (param_do_rtt)
        doRttTest();

    if (param_do_delay)
        doDelayTest();

    return 0;
}