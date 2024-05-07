#ifndef OFFLINE_H
#define OFFLINE_H

#include "stdint.h"
#include "ns3/queue-size.h"
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/single-flow-helper.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/traffic-control-helper.h"

#define TCP_DISCARD_PORT 10

const uint32_t DEFAULT_LINK_RATE = 20;                    // Gbps
const uint32_t DEFAULT_LINK_DELAY = 100;                  // us
const uint32_t DELAY_A_B = 50;                            // us
const uint32_t NUMBER_OF_EXPERIMENT_REPEATS_LONG = 30;    
const uint32_t NUMBER_OF_EXPERIMENT_REPEATS_SHORT = 5;   
const uint32_t BIG_FLOW_STEADY_TIME = 500;                // ms
const uint32_t RUNTIME = 1500;                            // ms
const uint32_t RUNTIME_LARGE = 5000;                      // ms
const uint32_t DEFAULT_MSS = 1460;
const uint32_t CHECK_SHORT_COMPLETTION_EACH = 10;         // ms

ns3::Ptr<ns3::SingleFlowApplication> shortFlowApplicationInstance = nullptr;
uint32_t doneCount = 0;

uint32_t systemCount = 1;
uint32_t systemId = 0;
uint32_t userId = 0;

const std::vector<uint32_t> input_rtts = 
    {4 * DEFAULT_LINK_DELAY, 6 * DEFAULT_LINK_DELAY, 8 * DEFAULT_LINK_DELAY};

const std::vector<double> input_packet_drops = 
    {0.0, 0.00005, 0.05, 0.0500475};

const std::vector<uint32_t> input_flow_sizes = 
    {10 * DEFAULT_MSS, 20 * DEFAULT_MSS, 30 * DEFAULT_MSS, 40 * DEFAULT_MSS, 50 * DEFAULT_MSS,
        60 * DEFAULT_MSS, 70 * DEFAULT_MSS, 80 * DEFAULT_MSS, 90 * DEFAULT_MSS, 100 * DEFAULT_MSS};

const std::vector<double> input_utilizations = {
    0.9, 0.95, 1.0
};

const uint32_t N_LOW = 1000;
const uint32_t N_HIGH = 10000;
const uint32_t NUM_N = 40;
const uint32_t VERY_SHORT_FLOW_SIZE = 512;

void doTpTest();
void doRttTest();
void doDelayTest();
void checkShortIsDone(ns3::Ptr<ns3::Node> h1) {
    static uint16_t port_start =1000;
    if (shortFlowApplicationInstance == nullptr)
        return;

    if (shortFlowApplicationInstance->IsDone()) {
        doneCount++;
        if (doneCount >= NUMBER_OF_EXPERIMENT_REPEATS_SHORT)
            ns3::Simulator::Stop(ns3::Seconds(0));
        else {
            std::cout << "[" << systemId << "]" << "DoneCount = " << doneCount << "\n";
            ns3::SingleFlowHelper shortHelper("ns3::TcpSocketFactory", ns3::InetSocketAddress("10.0.1.2", TCP_DISCARD_PORT));
            shortHelper.SetAttribute("PacketSize", ns3::UintegerValue(DEFAULT_MSS));
            shortHelper.SetAttribute("FlowSize", ns3::UintegerValue(VERY_SHORT_FLOW_SIZE + ns3::Simulator::Now().GetMilliSeconds() % 128));
            shortHelper.SetAttribute("Local", ns3::AddressValue(ns3::InetSocketAddress("10.0.0.1", port_start)));
            port_start++;
            ns3::ApplicationContainer shortApplication = shortHelper.Install(h1);
            shortFlowApplicationInstance = ns3::DynamicCast<ns3::SingleFlowApplication>(shortApplication.Get(0));
            shortFlowApplicationInstance->m_reportDone = true;
            shortApplication.Start(ns3::MilliSeconds(10));
        }
    }
}

void schedulePacketLoss(double loss_rate, ns3::NetDeviceContainer ds1s2) {

    // Error model
    ns3::Ptr<ns3::RateErrorModel> em = ns3::CreateObject<ns3::RateErrorModel>();
    em->SetRate(loss_rate);
    em->SetUnit(ns3::RateErrorModel::ERROR_UNIT_PACKET);
    ds1s2.Get(0)->SetAttribute("ReceiveErrorModel", ns3::PointerValue(em));
    ds1s2.Get(1)->SetAttribute("ReceiveErrorModel", ns3::PointerValue(em));
    NS_ASSERT(em->IsEnabled());
}

bool isCorrectIteration(uint32_t i) {
    return (i % systemCount) == systemId;
}

uint32_t getMFromN(uint32_t N, double u) {
    if (u == 1.0)
        return 0;
    return (N / u) - N;
}

std::vector<double> throughputAnalysis(
    double loss_rate, 
    uint32_t rtt);

std::vector<int> rttAnalysis(
    double loss_rate, 
    uint32_t rtt, 
    uint32_t flowSize);

std::vector<int> queueDelayAnalysis(
    uint32_t N, 
    uint32_t M);

void doGlobalConfigs() {
    ns3::Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", ns3::BooleanValue(true));
    ns3::Config::SetDefault("ns3::TcpL4Protocol::SocketType",
        ns3::TypeIdValue(ns3::TypeId::LookupByName("ns3::TcpDctcp")));
    ns3::Config::SetDefault("ns3::TcpSocket::SegmentSize", ns3::UintegerValue(6000));
    ns3::Config::SetDefault("ns3::PointToPointNetDevice::Mtu", ns3::UintegerValue(6000));
    ns3::GlobalValue::Bind ("ChecksumEnabled", ns3::BooleanValue (false));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::UseEcn", ns3::BooleanValue (true));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", ns3::BooleanValue (false));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", ns3::UintegerValue (6000));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::MaxSize", ns3::QueueSizeValue (ns3::QueueSize ("5000p")));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::QW", ns3::DoubleValue (1));
}


#endif /* OFFLINE_H */