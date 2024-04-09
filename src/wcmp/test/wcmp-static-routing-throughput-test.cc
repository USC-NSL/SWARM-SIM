#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/test.h"
#include "ns3/packet.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/simple-net-device.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/wcmp-static-routing-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/string.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/applications-module.h"
#include "ns3/application-container.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WcmpThroughputTest");

class WcmpStaticRoutingThroughputTest : public TestCase {
    public:
        WcmpStaticRoutingThroughputTest();
        ~WcmpStaticRoutingThroughputTest() override;

        Ptr<Packet> m_receivedPacket;

        void DoSendData(Ptr<Socket> socket, std::string to);
        void SendData(Ptr<Socket> socket, std::string to, double t);
        void ReceivePkt(Ptr<Socket> socket);
        void unicastTest(Ptr<Node> sender, Ptr<Node> receiver, std::string receiverIpStr, double when);
        void emitAtRegularIntervals(Ptr<Node> sender, std::string to, uint32_t n, double start, double delta);
        void DoAdjustWeights(Ptr<Node> wcmpNode);
        void AdjustWeightsAt(Ptr<Node> wcmpNode, double when);
        void setInterfaceDown(Ptr<Node> node, uint32_t if_index);
        void reportRxAt(ApplicationContainer sinkApps, double when);
        void DoReportRx(ApplicationContainer sinkApps);

        typedef void (*send_method)(Ptr<Socket>, std::string);
        typedef void (*recv_method)(Ptr<Socket>);

        void DoRun() override;

    private:
        std::vector<uint32_t> rxCounts{0, 0, 0};
        void addFabricInterfaces(std::vector<NetDeviceContainer>);
        void setupAnimation(std::vector<Ptr<Node>>);
        AnimationInterface *anim = nullptr;
};


WcmpStaticRoutingThroughputTest :: WcmpStaticRoutingThroughputTest() 
    : TestCase("WCMP throughput test")
{
}

WcmpStaticRoutingThroughputTest :: ~WcmpStaticRoutingThroughputTest() {
}

void
WcmpStaticRoutingThroughputTest :: ReceivePkt(Ptr<Socket> socket)
{
    uint32_t availableData [[maybe_unused]] = socket->GetRxAvailable();
    m_receivedPacket = socket->Recv(std::numeric_limits<uint32_t>::max(), 0);
    NS_TEST_ASSERT_MSG_EQ(availableData,
                          m_receivedPacket->GetSize(),
                          "Received packet size is not equal to Rx buffer size");
    socket->Close();
}

void
WcmpStaticRoutingThroughputTest :: DoSendData(Ptr<Socket> socket, std::string to)
{
    Address realTo = InetSocketAddress(Ipv4Address(to.c_str()), 1234);
    NS_TEST_EXPECT_MSG_EQ(socket->SendTo(Create<Packet>(123), 0, realTo), 123, "100");
    // For UDP sockets, we just close them afterward
    socket->Close();
}

void
WcmpStaticRoutingThroughputTest :: setInterfaceDown(Ptr<Node> node, uint32_t if_index)
{
    node->GetObject<Ipv4>()->SetDown(if_index);
}

void
WcmpStaticRoutingThroughputTest :: SendData(Ptr<Socket> socket, std::string to, double t)
{
    m_receivedPacket = Create<Packet>();
    Simulator::ScheduleWithContext(socket->GetNode()->GetId(),
                                   Seconds(t),
                                   &WcmpStaticRoutingThroughputTest::DoSendData,
                                   this,
                                   socket,
                                   to);
}

void
WcmpStaticRoutingThroughputTest :: DoReportRx(ApplicationContainer sinkApps) {
    for (uint32_t i = 0; i < 3; i++) {
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        NS_LOG_INFO("Count of RX on L" << i << " since last call:" << sink->GetTotalRx() - rxCounts[i]);
        rxCounts[i] = sink->GetTotalRx();
    }
}

void 
WcmpStaticRoutingThroughputTest :: reportRxAt(ApplicationContainer sinkApps, double when) {
    Simulator::Schedule(Seconds(when),
                                   &WcmpStaticRoutingThroughputTest::DoReportRx,
                                   this,
                                   sinkApps);
}

void
WcmpStaticRoutingThroughputTest :: unicastTest(Ptr<Node> sender, Ptr<Node> receiver, std::string receiverIpStr, double when) {
    // Create the UDP receiver socket
    Ptr<SocketFactory> rxSocketFactory = receiver->GetObject<UdpSocketFactory>();
    Ptr<Socket> rxSocket = rxSocketFactory->CreateSocket();
    
    NS_TEST_EXPECT_MSG_EQ(rxSocket->Bind(InetSocketAddress(Ipv4Address(receiverIpStr.c_str()), 1234)), 0, "Did not bind correctly");

    // Set callback for receiving the packet on the receiver
    rxSocket->SetRecvCallback(MakeCallback(&WcmpStaticRoutingThroughputTest::ReceivePkt, this));

    // Create a UDP sender socket
    Ptr<SocketFactory> txSocketFactory = sender->GetObject<UdpSocketFactory>();
    Ptr<Socket> txSocket = txSocketFactory->CreateSocket();
    txSocket->SetAllowBroadcast(true);

    // Send some data at t=`when`, and see if it does arrive
    SendData(txSocket, receiverIpStr, when);
}

void 
WcmpStaticRoutingThroughputTest :: emitAtRegularIntervals(Ptr<Node> sender, std::string to, uint32_t n, double start, double delta) {
    Ptr<SocketFactory> txSocketFactory = sender->GetObject<UdpSocketFactory>();
    for (uint32_t i = 0; i < n; i++) {
        Ptr<Socket> txSocket = txSocketFactory->CreateSocket();
        txSocket->SetAllowBroadcast(true);
        SendData(txSocket, to, start + delta * i);
        txSocket->Close();
    }
}

void WcmpStaticRoutingThroughputTest :: DoAdjustWeights(Ptr<Node> wcmpNode) {
    WcmpStaticRoutingHelper wcmp;
    Ptr<Ipv4> ipv4 = wcmpNode->GetObject<Ipv4>();
    wcmp.SetInterfaceWeight(ipv4, 3, 2);
    wcmp.SetInterfaceWeight(ipv4, 4, 3);
}

void WcmpStaticRoutingThroughputTest :: AdjustWeightsAt(Ptr<Node> wcmpNode, double when) {
    Simulator::ScheduleWithContext(wcmpNode->GetId(),
                                   Seconds(when),
                                   &WcmpStaticRoutingThroughputTest::DoAdjustWeights,
                                   this,
                                   wcmpNode);
}

void
WcmpStaticRoutingThroughputTest :: setupAnimation(std::vector<Ptr<Node>> nodes) {
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    for (int i = 0; i < 5; i++)
        mobility.Install(NodeContainer(nodes[i]));

    this->anim = new AnimationInterface("swarm-anim.xml");
    // nodes are A, R, Li respectively
    this->anim->SetConstantPosition(nodes[0], 0.0, 10.0);
    this->anim->SetConstantPosition(nodes[1], 10.0, 10.0);
    this->anim->SetConstantPosition(nodes[2], 20.0, 20.0);
    this->anim->SetConstantPosition(nodes[3], 20.0, 10.0);
    this->anim->SetConstantPosition(nodes[4], 20.0, 0.0);

    this->anim->UpdateNodeDescription(nodes[0], "A");
    this->anim->UpdateNodeDescription(nodes[1], "R");
    this->anim->UpdateNodeDescription(nodes[2], "L1");
    this->anim->UpdateNodeDescription(nodes[3], "L2");
    this->anim->UpdateNodeDescription(nodes[4], "L3");
}

/**
 * This is a simple series of tests to see if WCMP integrated correctly
 * We create the following network:
 * 
 *                         t    2.0    3.0    4.0
 *                      ____________________________
 *        
 *           +-- L1       w1    1      1      1
 *          /       
 *  A ---- R --- L2       w2    1      2      X
 *          \       
 *           +-- L3       w3    1      3      3
 * 
 * Here, R will have a WCMP stack and A will send packets towards B.
 * A will use a UDP socket to send these packets. We will test WCMP
 * compliance with TCP later.
 * 
 * - During [1.0, 2.0), R will do ECMP between its 3 outgoing links
 * - During [2.0, 3.0), we will adjust the weights and distribute the packets 1:2:3
 * - During [3.0, 4.0), we will bring down the interface from R to L2, we expect no packet loss
 *   with NS-3, since it adjusts the routing table instantly.
 * 
 * The test ends at t = 4.0. During all of this, we measure throughput
 * on the ingress of L1, L2 and L3, and then we will use that to see if
 * the weights are adjusted correctly.
 * 
 * The node A will have an IP address of `10.0.0.1/32` and all 3 nodes
 * Li will have an IP address of `10.0.0.2/32`.
*/

void
WcmpStaticRoutingThroughputTest :: addFabricInterfaces(std::vector<NetDeviceContainer> devices) {
    Ptr<Ipv4> ipv4;
    uint32_t ifindex;
    for (auto const & it : devices) {
        for (uint32_t i = 0; i < it.GetN(); i++) {
            ipv4 = it.Get(i)->GetNode()->GetObject<Ipv4>();
            ifindex = ipv4->AddInterface(it.Get(i));
            ipv4->AddAddress(ifindex, Ipv4InterfaceAddress(Ipv4Address("127.0.0.1"), Ipv4Mask("/8")));
            ipv4->SetUp(ifindex);
        }
    }
}

void
WcmpStaticRoutingThroughputTest :: DoRun()
{
    Time::SetResolution(Time::US);

    Ptr<Node> A = CreateObject<Node>();
    Ptr<Node> R = CreateObject<Node>();
    Ptr<Node> L1 = CreateObject<Node>();
    Ptr<Node> L2 = CreateObject<Node>();
    Ptr<Node> L3 = CreateObject<Node>();

    NodeContainer normal = NodeContainer(A, L1, L2, L3);

    // Install basic Ipv4 static routing
    InternetStackHelper internet;
    internet.Install(normal);

    // Install the WCMP stack on R
    WcmpStaticRoutingHelper wcmpHelper;
    Ipv4StaticRoutingHelper staticHelper;
    Ipv4ListRoutingHelper listHelper;
    InternetStackHelper interetHelper;

    listHelper.Add(staticHelper, 0);
    listHelper.Add(wcmpHelper, -20);
    interetHelper.SetRoutingHelper(listHelper);
    interetHelper.Install(R);

    // simple links
    NodeContainer AR = NodeContainer(A, R);
    NodeContainer RL1 = NodeContainer(R, L1);
    NodeContainer RL2 = NodeContainer(R, L2);
    NodeContainer RL3 = NodeContainer(R, L3);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("55ms"));
    NetDeviceContainer dAR = p2p.Install(AR);
    NetDeviceContainer dRL1 = p2p.Install(RL1);
    NetDeviceContainer dRL2 = p2p.Install(RL2);
    NetDeviceContainer dRL3 = p2p.Install(RL3);

    // Add endpoint devices
    Ptr<SimpleNetDevice> deviceA = CreateObject<SimpleNetDevice>();
    deviceA->SetAddress(Mac48Address::Allocate());
    A->AddDevice(deviceA);

    Ptr<SimpleNetDevice> deviceL1 = CreateObject<SimpleNetDevice>();
    deviceL1->SetAddress(Mac48Address::Allocate());
    L1->AddDevice(deviceL1);
    Ptr<SimpleNetDevice> deviceL2 = CreateObject<SimpleNetDevice>();
    deviceL2->SetAddress(Mac48Address::Allocate());
    L2->AddDevice(deviceL2);
    Ptr<SimpleNetDevice> deviceL3 = CreateObject<SimpleNetDevice>();
    deviceL3->SetAddress(Mac48Address::Allocate());
    L3->AddDevice(deviceL3);

    // Add fabric interfaces without an IP address
    this->addFabricInterfaces(std::vector<NetDeviceContainer>{
        dAR,
        dRL1, dRL2, dRL3
    });

    // Add IP addresses
    Ptr<Ipv4> ipv4A = A->GetObject<Ipv4>();
    
    Ptr<Ipv4> ipv4L1 = L1->GetObject<Ipv4>();
    Ptr<Ipv4> ipv4L2 = L2->GetObject<Ipv4>();
    Ptr<Ipv4> ipv4L3 = L3->GetObject<Ipv4>();

    int32_t ifIndexA = ipv4A->AddInterface(deviceA);
    int32_t ifIndexL1 = ipv4L1->AddInterface(deviceL1);
    int32_t ifIndexL2 = ipv4L2->AddInterface(deviceL2);
    int32_t ifIndexL3 = ipv4L3->AddInterface(deviceL3);

    Ipv4Address ipA = Ipv4Address("10.0.0.1");
    Ipv4Address ipDst = Ipv4Address("10.0.0.2");
    Ipv4Mask maskOnes = Ipv4Mask("/32");

    Ipv4InterfaceAddress ifInAddrA =
        Ipv4InterfaceAddress(ipA, maskOnes);
    ipv4A->AddAddress(ifIndexA, ifInAddrA);
    ipv4A->SetMetric(ifIndexA, 1);
    ipv4A->SetUp(ifIndexA);

    Ipv4InterfaceAddress ifInAddrL1 =
        Ipv4InterfaceAddress(ipDst, maskOnes);
    ipv4L1->AddAddress(ifIndexL1, ifInAddrL1);
    ipv4L1->SetMetric(ifIndexL1, 1);
    ipv4L1->SetUp(ifIndexL1);
    
    Ipv4InterfaceAddress ifInAddrL2 =
        Ipv4InterfaceAddress(ipDst, maskOnes);
    ipv4L2->AddAddress(ifIndexL2, ifInAddrL2);
    ipv4L2->SetMetric(ifIndexL2, 1);
    ipv4L2->SetUp(ifIndexL2);

    Ipv4InterfaceAddress ifInAddrL3 =
        Ipv4InterfaceAddress(ipDst, maskOnes);
    ipv4L3->AddAddress(ifIndexL3, ifInAddrL3);
    ipv4L3->SetMetric(ifIndexL3, 1);
    ipv4L3->SetUp(ifIndexL3);

    // Create static routes on A and Li
    Ptr<wcmp::WcmpStaticRouting> wcmp;
    Ptr<Ipv4StaticRouting> stat;

    // For A
    stat = staticHelper.GetStaticRouting(ipv4A);
    stat->AddHostRouteTo(ipDst, 1);

    // For Li
    stat = staticHelper.GetStaticRouting(L1->GetObject<Ipv4>());
    stat->AddHostRouteTo(ipDst, 2);
    stat = staticHelper.GetStaticRouting(L2->GetObject<Ipv4>());
    stat->AddHostRouteTo(ipDst, 2);
    stat = staticHelper.GetStaticRouting(L3->GetObject<Ipv4>());
    stat->AddHostRouteTo(ipDst, 2);

    // For R
    wcmp = wcmpHelper.GetWcmpStaticRouting(R->GetObject<Ipv4>());
    wcmp->AddNetworkRouteTo(ipDst, maskOnes, 2, 1);
    wcmp->AddNetworkRouteTo(ipDst, maskOnes, 3, 1);
    wcmp->AddNetworkRouteTo(ipDst, maskOnes, 4, 1);

    // Animation
    this->setupAnimation(std::vector<Ptr<Node>>{
        A, R, L1, L2, L3
    });

    Ptr<OutputStreamWrapper> routingStream =
        Create<OutputStreamWrapper>("wcmp.routes", std::ios::out);
    Ipv4ListRoutingHelper::PrintRoutingTableAt(Seconds(2.1), R, routingStream);
    Ipv4ListRoutingHelper::PrintRoutingTableAt(Seconds(3.1), R, routingStream);
    Ipv4ListRoutingHelper::PrintRoutingTableAt(Seconds(4.1), R, routingStream);

    // Create sink on L1, L2, and L3
    PacketSinkHelper sink("ns3::UdpSocketFactory",
        Address(InetSocketAddress(Ipv4Address("10.0.0.2"), 1234)));
    ApplicationContainer sinkApp = sink.Install(NodeContainer(L1, L2, L3));
    sinkApp.Start(Seconds(1.5));
    sinkApp.Stop(Seconds(5.0));

    // ECMP test
    emitAtRegularIntervals(A, "10.0.0.2", 300, 2.0, 0.01);
    
    // OnOffHelper onoff("ns3::UdpSocketFactory",
    //                   Address(InetSocketAddress(Ipv4Address("10.0.0.2"), 1234)));
    // ApplicationContainer sendApp = onoff.Install(A);
    // sendApp.Start(Seconds(2.0));
    // sendApp.Stop(Seconds(5.0));

    // WCMP test
    AdjustWeightsAt(R, 3.0);

    // Bring down the link between R--L2 at t=4.0
    Simulator::ScheduleWithContext(
        R->GetId(),
        Seconds(4),
        &WcmpStaticRoutingThroughputTest::setInterfaceDown,
        this, R, 3
    );

    // Flow monitor
    // FlowMonitorHelper flowHelper;
    // Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    reportRxAt(sinkApp, 3.0);
    reportRxAt(sinkApp, 4.0);
    reportRxAt(sinkApp, 5.0);

    ns3::LogComponentEnable("WcmpThroughputTest", LOG_LEVEL_INFO);
    
    Simulator::Stop(Seconds(5));
    Simulator::Run();
    // monitor->SerializeToXmlFile("wcmp-flows.xml", true, false);
    Simulator::Destroy();
}

class WcmpStaticRoutingTestSuite : public TestSuite
{
    public:
        WcmpStaticRoutingTestSuite();
};

WcmpStaticRoutingTestSuite::WcmpStaticRoutingTestSuite()
    : TestSuite("wcmp-static-routing", UNIT)
{
    AddTestCase(new WcmpStaticRoutingThroughputTest(), TestCase::QUICK);
}

static WcmpStaticRoutingTestSuite wcmpStaticRoutingTestSuite;
