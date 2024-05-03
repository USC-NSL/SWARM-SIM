#include "mpi-flow-monitor.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <fstream>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MpiFlowMonitor");

NS_OBJECT_ENSURE_REGISTERED(MpiFlowMonitor);

TypeId
MpiFlowMonitor::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::MpiFlowMonitor")
            .SetParent<Object>()
            .SetGroupName("MpiFlowMonitor")
            .AddConstructor<MpiFlowMonitor>()
            .AddAttribute(
                "MaxPerHopDelay",
                ("The maximum per-hop delay that should be considered.  "
                 "Packets still not received after this delay are to be considered lost."),
                TimeValue(Seconds(10.0)),
                MakeTimeAccessor(&MpiFlowMonitor::m_maxPerHopDelay),
                MakeTimeChecker())
            .AddAttribute("StartTime",
                          ("The time when the monitoring starts."),
                          TimeValue(Seconds(0.0)),
                          MakeTimeAccessor(&MpiFlowMonitor::Start),
                          MakeTimeChecker())
            .AddAttribute(
                "FlowInterruptionsMinTime",
                ("The minimum inter-arrival time that is considered a flow interruption."),
                TimeValue(Seconds(0.5)),
                MakeTimeAccessor(&MpiFlowMonitor::m_flowInterruptionsMinTime),
                MakeTimeChecker());
    return tid;
}

TypeId
MpiFlowMonitor::GetInstanceTypeId() const
{
    return GetTypeId();
}

MpiFlowMonitor::MpiFlowMonitor()
    : m_enabled(false),
      m_systemId(0)
{
    NS_LOG_FUNCTION(this);
}

void
MpiFlowMonitor::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Simulator::Cancel(m_startEvent);
    Simulator::Cancel(m_stopEvent);
    for (auto iter = m_classifiers.begin(); iter != m_classifiers.end(); iter++)
    {
        *iter = nullptr;
    }
    for (uint32_t i = 0; i < m_flowProbes.size(); i++)
    {
        m_flowProbes[i]->Dispose();
        m_flowProbes[i] = nullptr;
    }
    Object::DoDispose();
}

inline MpiFlowMonitor::FlowStats&
MpiFlowMonitor::GetStatsForFlow(FlowId flowId)
{
    NS_LOG_FUNCTION(this);
    auto iter = m_flowStats.find(flowId);
    if (iter == m_flowStats.end())
    {
        MpiFlowMonitor::FlowStats& ref = m_flowStats[flowId];
        ref.txBytes = 0;
        ref.rxBytes = 0;
        return ref;
    }
    else
    {
        return iter->second;
    }
}

void
MpiFlowMonitor::ReportFirstTx(Ptr<MpiFlowProbe> probe,
                           uint32_t flowId,
                           uint32_t packetId,
                           uint32_t packetSize)
{
    NS_LOG_FUNCTION(this << probe << flowId << packetId << packetSize);
    if (!m_enabled)
    {
        NS_LOG_DEBUG("MpiFlowMonitor not enabled; returning");
        return;
    }
    Time now = Simulator::Now();
    TrackedPacket& tracked = m_trackedPackets[std::make_pair(flowId, packetId)];
    tracked.firstSeenTime = now;
    tracked.lastSeenTime = tracked.firstSeenTime;
    probe->AddPacketStats(flowId, packetSize, Seconds(0));

    FlowStats& stats = GetStatsForFlow(flowId);
    if (stats.txBytes == 0)
    {
        stats.timeFirstTxPacket = now;
    }
    stats.txBytes += packetSize;
    stats.timeLastTxPacket = now;
}

void
MpiFlowMonitor::ReportForwarding(
    Ptr<MpiFlowProbe> probe,
    uint32_t flowId,
    uint32_t packetId,
    uint32_t packetSize, 
    uint64_t tStart, 
    uint64_t tLastRx)
{
    NS_ABORT_MSG("Forwarding check not yet implement!");
}

void
MpiFlowMonitor::ReportLastRx(
    Ptr<MpiFlowProbe> probe,
    uint32_t flowId,
    uint32_t packetId,
    uint32_t packetSize,
    uint64_t tStart, 
    uint64_t tLastRx)
{
    NS_LOG_FUNCTION(this << probe << flowId << packetId << packetSize);
    if (!m_enabled)
    {
        NS_LOG_DEBUG("MpiFlowMonitor not enabled; returning");
        return;
    }

    Time now = Simulator::Now();
    Time delay = (now - Time::FromInteger(tStart, Time::NS));
    probe->AddPacketStats(flowId, packetSize, delay);

    FlowStats& stats = GetStatsForFlow(flowId);

    if (stats.timeFirstTxPacket.ToInteger(Time::GetResolution()) == 0)
        stats.timeFirstTxPacket = Time::FromInteger(tStart, Time::GetResolution());

    if (stats.rxBytes == 1)
    {
        stats.timeFirstRxPacket = now;
    }
    stats.rxBytes += packetSize;
    stats.timeLastRxPacket = now;

    NS_LOG_DEBUG("ReportLastTx: removing tracked packet (flowId=" << flowId << ", packetId="
                                                                  << packetId << ").");

    auto tracked = m_trackedPackets.find(std::make_pair(flowId, packetId));
    if (tracked != m_trackedPackets.end())
        m_trackedPackets.erase(tracked); // we don't need to track this packet anymore
}

void
MpiFlowMonitor::ReportDrop(
    Ptr<MpiFlowProbe> probe,
    uint32_t flowId,
    uint32_t packetId,
    uint32_t packetSize,
    uint32_t reasonCode)
{
    NS_LOG_FUNCTION(this << probe << flowId << packetId << packetSize << reasonCode);
    if (!m_enabled)
    {
        NS_LOG_DEBUG("MpiFlowMonitor not enabled; returning");
        return;
    }

    probe->AddPacketDropStats(flowId, packetSize, reasonCode);

    FlowStats& stats = GetStatsForFlow(flowId);
    if (stats.packetsDropped.size() < reasonCode + 1)
    {
        stats.packetsDropped.resize(reasonCode + 1, 0);
        stats.bytesDropped.resize(reasonCode + 1, 0);
    }
    ++stats.packetsDropped[reasonCode];
    stats.bytesDropped[reasonCode] += packetSize;
    NS_LOG_DEBUG("++stats.packetsDropped["
                 << reasonCode << "]; // becomes: " << stats.packetsDropped[reasonCode]);

    auto tracked = m_trackedPackets.find(std::make_pair(flowId, packetId));
    if (tracked != m_trackedPackets.end())
    {
        // we don't need to track this packet anymore
        // FIXME: this will not necessarily be true with broadcast/multicast
        NS_LOG_DEBUG("ReportDrop: removing tracked packet (flowId=" << flowId << ", packetId="
                                                                    << packetId << ").");
        m_trackedPackets.erase(tracked);
    }
}

const MpiFlowMonitor::FlowStatsContainer&
MpiFlowMonitor::GetFlowStats() const
{
    return m_flowStats;
}

void
MpiFlowMonitor::NotifyConstructionCompleted()
{
    Object::NotifyConstructionCompleted();
}

void
MpiFlowMonitor::AddProbe(Ptr<MpiFlowProbe> probe)
{
    m_flowProbes.push_back(probe);
}

const MpiFlowMonitor::FlowProbeContainer&
MpiFlowMonitor::GetAllProbes() const
{
    return m_flowProbes;
}

void
MpiFlowMonitor::Start(const Time& time)
{
    NS_LOG_FUNCTION(this << time.As(Time::S));
    if (m_enabled)
    {
        NS_LOG_DEBUG("MpiFlowMonitor already enabled; returning");
        return;
    }
    Simulator::Cancel(m_startEvent);
    NS_LOG_DEBUG("Scheduling start at " << time.As(Time::S));
    m_startEvent = Simulator::Schedule(time, &MpiFlowMonitor::StartRightNow, this);
}

void
MpiFlowMonitor::Stop(const Time& time)
{
    NS_LOG_FUNCTION(this << time.As(Time::S));
    Simulator::Cancel(m_stopEvent);
    NS_LOG_DEBUG("Scheduling stop at " << time.As(Time::S));
    m_stopEvent = Simulator::Schedule(time, &MpiFlowMonitor::StopRightNow, this);
}

void
MpiFlowMonitor::StartRightNow()
{
    NS_LOG_FUNCTION(this);
    if (m_enabled)
    {
        NS_LOG_DEBUG("FlowMonitor already enabled; returning");
        return;
    }
    m_enabled = true;
}

void
MpiFlowMonitor::StopRightNow()
{
    NS_LOG_FUNCTION(this);
    if (!m_enabled)
    {
        NS_LOG_DEBUG("MpiFlowMonitor not enabled; returning");
        return;
    }
    m_enabled = false;
}

void
MpiFlowMonitor::AddFlowClassifier(Ptr<MpiFlowClassifier> classifier)
{
    m_classifiers.push_back(classifier);
}

void
MpiFlowMonitor::SerializeToXmlStream(
    std::ostream& os,
    uint16_t indent,
    bool enableHistograms,
    bool enableProbes)
{
    NS_LOG_FUNCTION(this << indent << enableHistograms << enableProbes);

    os << std::string(indent, ' ') << "<FlowMonitor>\n";
    indent += 2;
    os << std::string(indent, ' ') << "<FlowStats>\n";
    indent += 2;
    for (auto flowI = m_flowStats.begin(); flowI != m_flowStats.end(); flowI++)
    {
        if (flowI->second.timeFirstTxPacket.GetInteger() == 0 || flowI->second.timeLastRxPacket.GetInteger() == 0)
            continue;
            
        os << std::string(indent, ' ');
#define ATTRIB(name) << " " #name "=\"" << flowI->second.name << "\""
#define ATTRIB_TIME(name) << " " #name "=\"" << flowI->second.name.As(Time::NS) << "\""
        os << "<Flow flowId=\"" << flowI->first
           << "\"" ATTRIB_TIME(timeFirstTxPacket) ATTRIB_TIME(timeFirstRxPacket)
                  ATTRIB_TIME(timeLastTxPacket) ATTRIB_TIME(timeLastRxPacket) 
                       ATTRIB(txBytes) ATTRIB(rxBytes)
           << ">\n";
#undef ATTRIB_TIME
#undef ATTRIB

        indent += 2;
        for (uint32_t reasonCode = 0; reasonCode < flowI->second.packetsDropped.size();
             reasonCode++)
        {
            os << std::string(indent, ' ');
            os << "<packetsDropped reasonCode=\"" << reasonCode << "\""
               << " number=\"" << flowI->second.packetsDropped[reasonCode] << "\" />\n";
        }
        for (uint32_t reasonCode = 0; reasonCode < flowI->second.bytesDropped.size(); reasonCode++)
        {
            os << std::string(indent, ' ');
            os << "<bytesDropped reasonCode=\"" << reasonCode << "\""
               << " bytes=\"" << flowI->second.bytesDropped[reasonCode] << "\" />\n";
        }
        indent -= 2;

        os << std::string(indent, ' ') << "</Flow>\n";
    }
    indent -= 2;
    os << std::string(indent, ' ') << "</FlowStats>\n";

    for (auto iter = m_classifiers.begin(); iter != m_classifiers.end(); iter++)
    {
        (*iter)->SerializeToXmlStream(os, indent);
    }

    if (enableProbes)
    {
        os << std::string(indent, ' ') << "<FlowProbes>\n";
        indent += 2;
        for (uint32_t i = 0; i < m_flowProbes.size(); i++)
        {
            m_flowProbes[i]->SerializeToXmlStream(os, indent, i);
        }
        indent -= 2;
        os << std::string(indent, ' ') << "</FlowProbes>\n";
    }

    indent -= 2;
    os << std::string(indent, ' ') << "</FlowMonitor>\n";
}

std::string
MpiFlowMonitor::SerializeToXmlString(uint16_t indent, bool enableHistograms, bool enableProbes)
{
    NS_LOG_FUNCTION(this << indent << enableHistograms << enableProbes);
    std::ostringstream os;
    SerializeToXmlStream(os, indent, enableHistograms, enableProbes);
    return os.str();
}

void
MpiFlowMonitor::SerializeToXmlFile(std::string fileName, bool enableHistograms, bool enableProbes)
{
    NS_LOG_FUNCTION(this << fileName << m_systemId << enableHistograms << enableProbes);

    std::string fileNameWithSystemId;
    std::string::size_type pos = fileName.find(".xml");
    if (pos != std::string::npos) {
        fileNameWithSystemId += fileName.substr(0, pos) + '-' + std::to_string(m_systemId) + ".xml";
    }
    else {
        fileNameWithSystemId += fileName + '-' + std::to_string(m_systemId) + ".xml";
    }

    std::ofstream os(fileNameWithSystemId, std::ios::out | std::ios::binary);
    os << "<?xml version=\"1.0\" ?>\n";
    SerializeToXmlStream(os, 0, enableHistograms, enableProbes);
    os.close();
}

void
MpiFlowMonitor::ResetAllStats()
{
    NS_LOG_FUNCTION(this);

    for (auto& iter : m_flowStats)
    {
        auto& flowStat = iter.second;
        flowStat.txBytes = 0;
        flowStat.rxBytes = 0;
        flowStat.bytesDropped.clear();
        flowStat.packetsDropped.clear();
    }
}

} // namespace ns3
