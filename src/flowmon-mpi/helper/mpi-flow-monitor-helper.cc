#include "mpi-flow-monitor-helper.h"

#include "ns3/mpi-flow-monitor.h"
#include "ns3/ipv4-mpi-flow-classifier.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv6-l3-protocol.h"
#include "ns3/node-list.h"
#include "ns3/node.h"

namespace ns3
{

MpiFlowMonitorHelper::MpiFlowMonitorHelper()
{
    NS_OBJECT_ENSURE_REGISTERED(Ipv4MpiFlowProbeTag);
    m_monitorFactory.SetTypeId("ns3::MpiFlowMonitor");
}

MpiFlowMonitorHelper::~MpiFlowMonitorHelper()
{
    if (m_flowMonitor)
    {
        m_flowMonitor->Dispose();
        m_flowMonitor = nullptr;
        m_flowClassifier4 = nullptr;
    }
}

void
MpiFlowMonitorHelper::SetMonitorAttribute(std::string n1, const AttributeValue& v1)
{
    m_monitorFactory.Set(n1, v1);
}

Ptr<MpiFlowMonitor>
MpiFlowMonitorHelper::GetMonitor()
{
    if (!m_flowMonitor)
    {
        m_flowMonitor = m_monitorFactory.Create<MpiFlowMonitor>();
        m_flowMonitor->SetSystemId(GetSystemId());
        m_flowClassifier4 = Create<Ipv4MpiFlowClassifier>();
        m_flowClassifier4->SetSystemId(GetSystemId());
        m_flowMonitor->AddFlowClassifier(m_flowClassifier4);
    }
    return m_flowMonitor;
}

Ptr<MpiFlowClassifier>
MpiFlowMonitorHelper::GetClassifier()
{
    if (!m_flowClassifier4)
    {
        m_flowClassifier4 = Create<Ipv4MpiFlowClassifier>();
        m_flowClassifier4->SetSystemId(GetSystemId());
    }
    return m_flowClassifier4;
}

Ptr<MpiFlowMonitor>
MpiFlowMonitorHelper::Install(Ptr<Node> node)
{
    Ptr<MpiFlowMonitor> monitor = GetMonitor();
    Ptr<MpiFlowClassifier> classifier = GetClassifier();
    Ptr<Ipv4L3Protocol> ipv4 = node->GetObject<Ipv4L3Protocol>();
    if (ipv4)
    {
        Ptr<Ipv4MpiFlowProbe> probe =
            Create<Ipv4MpiFlowProbe>(monitor, DynamicCast<Ipv4MpiFlowClassifier>(classifier), node);
    }
    else {
        NS_FATAL_ERROR("NO IPv4!!!!!");
    }
    return m_flowMonitor;
}

Ptr<MpiFlowMonitor>
MpiFlowMonitorHelper::Install(NodeContainer nodes)
{
    for (auto i = nodes.Begin(); i != nodes.End(); ++i)
    {
        Ptr<Node> node = *i;
        if (node->GetObject<Ipv4L3Protocol>())
        {
            Install(node);
        }
        if (node->GetObject<Ipv6L3Protocol>()) {
            NS_ABORT_MSG("IPv6 not yet supported!");
        }
    }
    return m_flowMonitor;
}

Ptr<MpiFlowMonitor>
MpiFlowMonitorHelper::InstallAll()
{
    for (auto i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
        Ptr<Node> node = *i;
        if (node->GetObject<Ipv4L3Protocol>())
        {
            Install(node);
        }
    }
    return m_flowMonitor;
}

void
MpiFlowMonitorHelper::SerializeToXmlStream(
    std::ostream& os,
    uint16_t indent,
    bool enableHistograms,
    bool enableProbes)
{
    if (m_flowMonitor)
    {
        m_flowMonitor->SerializeToXmlStream(os, indent, enableHistograms, enableProbes);
    }
}

std::string
MpiFlowMonitorHelper::SerializeToXmlString(uint16_t indent, bool enableHistograms, bool enableProbes)
{
    std::ostringstream os;
    if (m_flowMonitor)
    {
        m_flowMonitor->SerializeToXmlStream(os, indent, enableHistograms, enableProbes);
    }
    return os.str();
}

void
MpiFlowMonitorHelper::SerializeToXmlFile(
    std::string fileName,
    bool enableHistograms,
    bool enableProbes)
{
    if (m_flowMonitor)
    {
        m_flowMonitor->SerializeToXmlFile(fileName, enableHistograms, enableProbes);
    }
}

} // namespace ns3
