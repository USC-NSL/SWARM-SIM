#include "mpi-flow-probe.h"
#include "mpi-flow-monitor.h"

namespace ns3
{

/* static */
TypeId
MpiFlowProbe::GetTypeId()
{
    static TypeId tid = 
        TypeId("ns3::MpiFlowProbe")
            .SetParent<Object>()
            .SetGroupName("MpiFlowMonitor")
        // No AddConstructor because this class has no default constructor.
        ;

    return tid;
}

MpiFlowProbe::~MpiFlowProbe()
{
}

MpiFlowProbe::MpiFlowProbe(Ptr<MpiFlowMonitor> flowMonitor)
    : m_flowMonitor(flowMonitor)
{
    m_flowMonitor->AddProbe(this);
}

void
MpiFlowProbe::DoDispose()
{
    m_flowMonitor = nullptr;
    Object::DoDispose();
}

void
MpiFlowProbe::AddPacketStats(FlowId flowId, uint32_t packetSize, Time delayFromFirstProbe)
{
    FlowStats& flow = m_stats[flowId];
    flow.delayFromFirstProbeSum += delayFromFirstProbe;
    flow.bytes += packetSize;
    ++flow.packets;
}

void
MpiFlowProbe::AddPacketDropStats(FlowId flowId, uint32_t packetSize, uint32_t reasonCode)
{
    FlowStats& flow = m_stats[flowId];

    if (flow.packetsDropped.size() < reasonCode + 1)
    {
        flow.packetsDropped.resize(reasonCode + 1, 0);
        flow.bytesDropped.resize(reasonCode + 1, 0);
    }
    ++flow.packetsDropped[reasonCode];
    flow.bytesDropped[reasonCode] += packetSize;
}

MpiFlowProbe::Stats
MpiFlowProbe::GetStats() const
{
    return m_stats;
}

void
MpiFlowProbe::SerializeToXmlStream(std::ostream& os, uint16_t indent, uint32_t index) const
{
    os << std::string(indent, ' ') << "<FlowProbe index=\"" << index << "\">\n";

    indent += 2;

    for (auto iter = m_stats.begin(); iter != m_stats.end(); iter++)
    {
        os << std::string(indent, ' ');
        os << "<FlowStats "
           << " flowId=\"" << iter->first << "\""
           << " packets=\"" << iter->second.packets << "\""
           << " bytes=\"" << iter->second.bytes << "\""
           << " delayFromFirstProbeSum=\"" << iter->second.delayFromFirstProbeSum << "\""
           << " >\n";
        indent += 2;
        for (uint32_t reasonCode = 0; reasonCode < iter->second.packetsDropped.size(); reasonCode++)
        {
            os << std::string(indent, ' ');
            os << "<packetsDropped reasonCode=\"" << reasonCode << "\""
               << " number=\"" << iter->second.packetsDropped[reasonCode] << "\" />\n";
        }
        for (uint32_t reasonCode = 0; reasonCode < iter->second.bytesDropped.size(); reasonCode++)
        {
            os << std::string(indent, ' ');
            os << "<bytesDropped reasonCode=\"" << reasonCode << "\""
               << " bytes=\"" << iter->second.bytesDropped[reasonCode] << "\" />\n";
        }
        indent -= 2;
        os << std::string(indent, ' ') << "</FlowStats>\n";
    }
    indent -= 2;
    os << std::string(indent, ' ') << "</FlowProbe>\n";
}

} // namespace ns3
