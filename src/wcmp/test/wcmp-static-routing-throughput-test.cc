#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/test.h"
#include "ns3/packet.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/simulator.h"
#include "ns3/simple-net-device.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/wcmp-static-routing-helper.h"


using namespace ns3;


class WcmpStaticRoutingThroughputTest : public TestCase {
    public:
        WcmpStaticRoutingThroughputTest();
        ~WcmpStaticRoutingThroughputTest() override;

        Ptr<Packet> m_receivedPacket;

        void DoSendData(Ptr<Socket> socket, std::string to);
        void SendData(Ptr<Socket> socket, std::string to);
        void ReceivePkt(Ptr<Socket> socket);

        typedef void (*send_method)(Ptr<Socket>, std::string);
        typedef void (*recv_method)(Ptr<Socket>);

    private:
        void DoRun() override;
        void addFabricInterfaces(std::vector<NetDeviceContainer>);
};


WcmpStaticRoutingThroughputTest :: WcmpStaticRoutingThroughputTest() 
    : TestCase("A throughput test to see if WCMP actually works")
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
}

void
WcmpStaticRoutingThroughputTest :: DoSendData(Ptr<Socket> socket, std::string to)
{
    Address realTo = InetSocketAddress(Ipv4Address(to.c_str()), 1234);
    NS_TEST_EXPECT_MSG_EQ(socket->SendTo(Create<Packet>(123), 0, realTo), 123, "100");
}

void
WcmpStaticRoutingThroughputTest :: SendData(Ptr<Socket> socket, std::string to)
{
    m_receivedPacket = Create<Packet>();
    Simulator::ScheduleWithContext(socket->GetNode()->GetId(),
                                   Seconds(60),
                                   &WcmpStaticRoutingThroughputTest::DoSendData,
                                   this,
                                   socket,
                                   to);
    Simulator::Stop(Seconds(66));
    Simulator::Run();
}

class WcmpStaticRoutingTestSuite : public TestSuite
{
  public:
    WcmpStaticRoutingTestSuite();
};

WcmpStaticRoutingTestSuite::WcmpStaticRoutingTestSuite()
    : TestSuite("wcmp-static-routing", UNIT)
{
    AddTestCase(new WcmpStaticRoutingTestSuite, TestCase::QUICK);
}

static WcmpStaticRoutingTestSuite wcmpStaticRoutingTestSuite;

/**
 * This is a simple series of tests to see if WCMP integrated correctly
 * We create the following network:
 * 
 *                             t    0.0    10.0   20.0
 *                             ____________________________
 *        
 *           +-- L1 --+        w1    1      1      1
 *          /          \
 *  A ---- R --- L2 --- B      w2    1      2      X
 *          \          /
 *           +-- L3 --+        w3    1      3      3
 * 
 * Here, R will have a WCMP stack and A will send packets towards B.
 * A will use a UDP socket to send these packets. We will test WCMP
 * compliance with TCP later.
 * 
 * - During [0.0, 10.0), R will do ECMP between its 3 outgoing links
 * - During [10.0, 20.0), we will adjust the weights and distribute the packets 1:2:3
 * - During [20.0, 30.0), we will bring down the interface from R to L2, we expect no packet loss
 *   with NS-3, since it adjusts the routing table instantly.
 * 
 * The test ends at t = 40.0. During all of this, we measure throughput
 * on the ingress of L1, L2 and L3, and then we will use that to see if
 * the weights are adjusted correctly.
 * 
 * The node A will have an IP address of `10.0.0.1/32` and the node
 * B will have an IP address of `10.0.0.2/32`. No other device in the
 * network will have any IP address.
*/

void
WcmpStaticRoutingThroughputTest :: addFabricInterfaces(std::vector<NetDeviceContainer> devices) {
    Ptr<Ipv4> ipv4;
    for (auto const & it : devices) {
        for (uint32_t i = 0; i < it.GetN(); i++) {
            ipv4 = it.Get(i)->GetNode()->GetObject<Ipv4>();
            ipv4->AddInterface(it.Get(i));
        }
    }
}

void
WcmpStaticRoutingThroughputTest :: DoRun()
{
    Ptr<Node> A = CreateObject<Node>();
    Ptr<Node> R = CreateObject<Node>();
    Ptr<Node> B = CreateObject<Node>();
    Ptr<Node> L1 = CreateObject<Node>();
    Ptr<Node> L2 = CreateObject<Node>();
    Ptr<Node> L3 = CreateObject<Node>();

    NodeContainer c = NodeContainer(A, R, B, L1, L2, L3);

    // Install basic Ipv4 static routing
    InternetStackHelper internet;
    internet.Install(c);

    // simple links
    NodeContainer AR = NodeContainer(A, R);
    NodeContainer RL1 = NodeContainer(R, L1);
    NodeContainer RL2 = NodeContainer(R, L2);
    NodeContainer RL3 = NodeContainer(R, L3);
    NodeContainer BL1 = NodeContainer(L1, B);
    NodeContainer BL2 = NodeContainer(L2, B);
    NodeContainer BL3 = NodeContainer(L3, B);

    SimpleNetDeviceHelper devHelper;
    NetDeviceContainer dAR = devHelper.Install(AR);
    NetDeviceContainer dRL1 = devHelper.Install(RL1);
    NetDeviceContainer dRL2 = devHelper.Install(RL2);
    NetDeviceContainer dRL3 = devHelper.Install(RL3);
    NetDeviceContainer dBL1 = devHelper.Install(BL1);
    NetDeviceContainer dBL2 = devHelper.Install(BL2);
    NetDeviceContainer dBL3 = devHelper.Install(BL3);

    // Add fabric interfaces without an IP address
    this->addFabricInterfaces(std::vector<NetDeviceContainer>{
        dAR,
        dRL1, dRL2, dRL3,
        dBL1, dBL2, dBL3
    });

    // Add endpoint devices
    Ptr<SimpleNetDevice> deviceA = CreateObject<SimpleNetDevice>();
    deviceA->SetAddress(Mac48Address::Allocate());
    A->AddDevice(deviceA);

    Ptr<SimpleNetDevice> deviceB = CreateObject<SimpleNetDevice>();
    deviceB->SetAddress(Mac48Address::Allocate());
    B->AddDevice(deviceB);

    // Add IP addresses
    Ptr<Ipv4> ipv4A = A->GetObject<Ipv4>();
    Ptr<Ipv4> ipv4B = B->GetObject<Ipv4>();
    int32_t ifIndexA = ipv4A->AddInterface(deviceA);
    int32_t ifIndexB = ipv4B->AddInterface(deviceB);

    Ipv4Address ipA = Ipv4Address("10.0.0.1");
    Ipv4Address ipB = Ipv4Address("10.0.0.2");
    Ipv4Mask maskOnes = Ipv4Mask("/32");

    Ipv4InterfaceAddress ifInAddrA =
        Ipv4InterfaceAddress(ipA, maskOnes);
    ipv4A->AddAddress(ifIndexA, ifInAddrA);
    ipv4A->SetMetric(ifIndexA, 1);
    ipv4A->SetUp(ifIndexA);

    Ipv4InterfaceAddress ifInAddrB =
        Ipv4InterfaceAddress(ipB, maskOnes);
    ipv4B->AddAddress(ifIndexB, ifInAddrB);
    ipv4B->SetMetric(ifIndexB, 1);
    ipv4B->SetUp(ifIndexB);

    WcmpStaticRoutingHelper wcmpHelper;
    Ipv4StaticRoutingHelper staticHelper;
    InternetStackHelper interetHelper;

    // Install the WCMP stack on R
    interetHelper.SetRoutingHelper(wcmpHelper);
    interetHelper.Install(R);

    // Create static routes on A, B and Li
    Ptr<wcmp::WcmpStaticRouting> wcmp;
    Ptr<Ipv4StaticRouting> stat;

    // For A
    stat = staticHelper.GetStaticRouting(ipv4A);
    stat->AddHostRouteTo(ipB, 1);

    // For Li
    stat = staticHelper.GetStaticRouting(L1->GetObject<Ipv4>());
    stat->AddHostRouteTo(ipB, 2);
    stat = staticHelper.GetStaticRouting(L2->GetObject<Ipv4>());
    stat->AddHostRouteTo(ipB, 2);
    stat = staticHelper.GetStaticRouting(L3->GetObject<Ipv4>());
    stat->AddHostRouteTo(ipB, 2);

    // For R
    wcmp = wcmpHelper.GetWcmpStaticRouting(R->GetObject<Ipv4>());
    wcmp->AddNetworkRouteTo(ipB, maskOnes, 2, 1);
    wcmp->AddNetworkRouteTo(ipB, maskOnes, 3, 1);
    wcmp->AddNetworkRouteTo(ipB, maskOnes, 4, 1);

    // Create the UDP sockets
    Ptr<SocketFactory> rxSocketFactory = B->GetObject<UdpSocketFactory>();
    Ptr<Socket> rxSocket = rxSocketFactory->CreateSocket();
    NS_TEST_EXPECT_MSG_EQ(rxSocket->Bind(InetSocketAddress(ipB, 1234)),
                          0,
                          "trivial");
    rxSocket->SetRecvCallback(MakeCallback(&WcmpStaticRoutingThroughputTest::ReceivePkt, this));

    Ptr<SocketFactory> txSocketFactory = A->GetObject<UdpSocketFactory>();
    Ptr<Socket> txSocket = txSocketFactory->CreateSocket();
    txSocket->SetAllowBroadcast(true);

    // ------ Now the tests ------------

    // Unicast test
    SendData(txSocket, "10.0.0.2");
    NS_TEST_EXPECT_MSG_EQ(m_receivedPacket->GetSize(),
                          123,
                          "Static routing with /32 did not deliver all packets.");

}  
