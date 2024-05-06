#ifndef OFFLINE_H
#define OFFLINE_H

#include "stdint.h"
#include "ns3/queue-size.h"
#include "ns3/core-module.h"

#define TCP_DISCARD_PORT 10

const uint32_t DEFAULT_LINK_RATE = 20;                    // Gbps
const uint32_t DEFAULT_LINK_DELAY = 100;                  // us
const uint32_t NUMBER_OF_EXPERIMENT_REPEATS_LONG = 30;    
const uint32_t NUMBER_OF_EXPERIMENT_REPEATS_SHORT = 30;   
const uint32_t BIG_FLOW_STEADY_TIME = 500;                // ms
const uint32_t RUNTIME = 1500;                            // ms
const uint32_t DEFAULT_MSS = 1460;

uint32_t systemCount = 1;
uint32_t systemId = 0;

const std::vector<uint32_t> input_rtts = 
    {4 * DEFAULT_LINK_DELAY, 6 * DEFAULT_LINK_DELAY, 8 * DEFAULT_LINK_DELAY};

const std::vector<double> input_packet_drops = 
    {0.0, 0.00005, 0.05, 0.0500475};

const std::vector<uint32_t> input_flow_sizes = 
    {10 * DEFAULT_MSS, 20 * DEFAULT_MSS, 30 * DEFAULT_MSS, 40 * DEFAULT_MSS, 50 * DEFAULT_MSS,
        60 * DEFAULT_MSS, 70 * DEFAULT_MSS, 80 * DEFAULT_MSS, 90 * DEFAULT_MSS, 100 * DEFAULT_MSS};

const std::vector<std::pair<uint32_t, uint32_t>> input_m_and_n = {
    std::make_pair(9, 1),     // 0.1
    std::make_pair(4, 1),     // 0.2
    std::make_pair(2, 1),     // 0.33
    std::make_pair(3, 2),     // 0.4
    std::make_pair(1, 1),     // 0.5
    std::make_pair(2, 3),     // 0.6
    std::make_pair(1, 2),     // 0.67
    std::make_pair(1, 4),     // 0.8
    std::make_pair(1, 9)      // 0.9
};


void doTpTest();
void doRttTest();
void doDelayTest();


bool isCorrectIteration(uint32_t i) {
    return (i % systemCount) == systemId;
}

std::vector<double> throughputAnalysis(
    double loss_rate, 
    uint32_t rtt, 
    uint32_t delay_a = DEFAULT_LINK_DELAY, 
    uint32_t delay_b = DEFAULT_LINK_DELAY);

std::vector<uint32_t> rttAnalysis(
    double loss_rate, 
    uint32_t rtt, 
    uint32_t flowSize, 
    uint32_t delay_a = DEFAULT_LINK_DELAY, 
    uint32_t delay_b = DEFAULT_LINK_DELAY);

std::vector<uint32_t> queueDelayAnalysis(
    uint32_t rtt, 
    uint32_t flowSize, 
    uint32_t M, 
    uint32_t N,
    uint32_t delay_a = DEFAULT_LINK_DELAY, 
    uint32_t delay_b = DEFAULT_LINK_DELAY);

void doGlobalConfigs() {
    ns3::Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", ns3::BooleanValue(true));
    ns3::Config::SetDefault("ns3::TcpL4Protocol::SocketType",
        ns3::TypeIdValue(ns3::TypeId::LookupByName("ns3::TcpDctcp")));
    ns3::Config::SetDefault("ns3::TcpSocket::SegmentSize", ns3::UintegerValue(7500));
    ns3::Config::SetDefault("ns3::PointToPointNetDevice::Mtu", ns3::UintegerValue(10000));
    ns3::GlobalValue::Bind ("ChecksumEnabled", ns3::BooleanValue (false));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::UseEcn", ns3::BooleanValue (true));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", ns3::BooleanValue (false));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", ns3::UintegerValue (7500));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::MaxSize", ns3::QueueSizeValue (ns3::QueueSize ("5000p")));
    ns3::Config::SetDefault ("ns3::RedQueueDisc::QW", ns3::DoubleValue (1));
}


#endif /* OFFLINE_H */