#include "ipv4-mpi-flow-probe.h"
#include "ipv4-mpi-flow-classifier.h"
#include "mpi-flow-monitor.h"

#include "ns3/config.h"
#include "ns3/flow-id-tag.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/tcp-header.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Ipv4MpiFlowProbe");


TypeId
Ipv4MpiFlowProbeTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::Ipv4MpiFlowProbeTag")
                            .SetParent<Tag>()
                            .SetGroupName("FlowMonitor")
                            .AddConstructor<Ipv4MpiFlowProbeTag>();
    return tid;
}

TypeId
Ipv4MpiFlowProbeTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
Ipv4MpiFlowProbeTag::GetSerializedSize() const
{
    return 4 + 4 + 4 + 8 + 16;
}

void
Ipv4MpiFlowProbeTag::Serialize(TagBuffer buf) const
{
    buf.WriteU32(m_flowId);
    buf.WriteU32(m_packetId);
    buf.WriteU32(m_packetSize);

    uint8_t tBuf[4];
    m_src.Serialize(tBuf);
    buf.Write(tBuf, 4);
    m_dst.Serialize(tBuf);
    buf.Write(tBuf, 4);

    buf.WriteU64(m_tStart);
    buf.WriteU64(m_tLastRx);
}

void
Ipv4MpiFlowProbeTag::Deserialize(TagBuffer buf)
{
    m_flowId = buf.ReadU32();
    m_packetId = buf.ReadU32();
    m_packetSize = buf.ReadU32();

    uint8_t tBuf[4];
    buf.Read(tBuf, 4);
    m_src = Ipv4Address::Deserialize(tBuf);
    buf.Read(tBuf, 4);
    m_dst = Ipv4Address::Deserialize(tBuf);

    m_tStart = buf.ReadU64();
    m_tLastRx = buf.ReadU64();
}

void
Ipv4MpiFlowProbeTag::Print(std::ostream& os) const
{
    os << "FlowId=" << m_flowId;
    os << " PacketId=" << m_packetId;
    os << " PacketSize=" << m_packetSize;
    os << " tStart=" << m_tStart;
    os << " tLastRx=" << m_tLastRx;
}

Ipv4MpiFlowProbeTag::Ipv4MpiFlowProbeTag()
    : Tag()
{
}

Ipv4MpiFlowProbeTag::Ipv4MpiFlowProbeTag(
    uint32_t flowId,
    uint32_t packetId,
    uint32_t packetSize,
    Ipv4Address src,
    Ipv4Address dst,
    uint64_t tStart,
    uint64_t tLastRx)
    : Tag(),
      m_flowId(flowId),
      m_packetId(packetId),
      m_packetSize(packetSize),
      m_src(src),
      m_dst(dst),
      m_tStart(tStart),
      m_tLastRx(tLastRx)
{
}

void
Ipv4MpiFlowProbeTag::SetFlowId(uint32_t id) {
    m_flowId = id;
}
void
Ipv4MpiFlowProbeTag::SetPacketId(uint32_t id) {
    m_packetId = id;
}
void
Ipv4MpiFlowProbeTag::SetPacketSize(uint32_t size) {
    m_packetSize = size;
}
void Ipv4MpiFlowProbeTag :: SettTStart(uint64_t tStart) {
    m_tStart = tStart;
}
void Ipv4MpiFlowProbeTag :: SetTLastRx(uint64_t tLastRx) {
    m_tLastRx = tLastRx;
}

uint32_t
Ipv4MpiFlowProbeTag::GetFlowId() const {
    return m_flowId;
}
uint32_t
Ipv4MpiFlowProbeTag::GetPacketId() const {
    return m_packetId;
}
uint32_t
Ipv4MpiFlowProbeTag::GetPacketSize() const {
    return m_packetSize;
}
bool
Ipv4MpiFlowProbeTag::IsSrcDstValid(Ipv4Address src, Ipv4Address dst) const {
    return ((m_src == src) && (m_dst == dst));
}
uint64_t 
Ipv4MpiFlowProbeTag :: GettTStart() const {
    return m_tStart;
}
uint64_t 
Ipv4MpiFlowProbeTag:: GetTLastRx() const {
    return m_tLastRx;
}

///////////////////////////////////////////
// Ipv4MpiFlowProbe class implementation //
///////////////////////////////////////////

Ipv4MpiFlowProbe::Ipv4MpiFlowProbe(Ptr<MpiFlowMonitor> monitor,
                             Ptr<Ipv4MpiFlowClassifier> classifier,
                             Ptr<Node> node)
    : MpiFlowProbe(monitor),
      m_classifier(classifier)
{
    NS_LOG_FUNCTION(this << node->GetId());

    m_ipv4 = node->GetObject<Ipv4L3Protocol>();

    if (!m_ipv4->TraceConnectWithoutContext(
        "SendOutgoing",
        MakeCallback(&Ipv4MpiFlowProbe::SendOutgoingLogger, Ptr<Ipv4MpiFlowProbe>(this))))
    {
        NS_FATAL_ERROR("trace fail");
    }
    if (!m_ipv4->TraceConnectWithoutContext(
        "UnicastForward",
        MakeCallback(&Ipv4MpiFlowProbe::ForwardLogger, Ptr<Ipv4MpiFlowProbe>(this))))
    {
        NS_FATAL_ERROR("trace fail");
    }
    if (!m_ipv4->TraceConnectWithoutContext(
        "LocalDeliver",
        MakeCallback(&Ipv4MpiFlowProbe::ForwardUpLogger, Ptr<Ipv4MpiFlowProbe>(this))))
    {
        NS_FATAL_ERROR("trace fail");
    }

    if (!m_ipv4->TraceConnectWithoutContext(
        "Drop",
        MakeCallback(&Ipv4MpiFlowProbe::DropLogger, Ptr<Ipv4MpiFlowProbe>(this))))
    {
        NS_FATAL_ERROR("trace fail");
    }

    std::ostringstream qd;
    qd << "/NodeList/" << node->GetId() << "/$ns3::TrafficControlLayer/RootQueueDiscList/*/Drop";
    Config::ConnectWithoutContextFailSafe(
        qd.str(),
        MakeCallback(&Ipv4MpiFlowProbe::QueueDiscDropLogger, Ptr<Ipv4MpiFlowProbe>(this)));

    // code copied from point-to-point-helper.cc
    std::ostringstream oss;
    oss << "/NodeList/" << node->GetId() << "/DeviceList/*/TxQueue/Drop";
    Config::ConnectWithoutContextFailSafe(
        oss.str(),
        MakeCallback(&Ipv4MpiFlowProbe::QueueDropLogger, Ptr<Ipv4MpiFlowProbe>(this)));
}

Ipv4MpiFlowProbe::~Ipv4MpiFlowProbe()
{
}

/* static */
TypeId
Ipv4MpiFlowProbe::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::Ipv4MpiFlowProbe")
            .SetParent<MpiFlowProbe>()
            .SetGroupName("MpiFlowMonitor")
        // No AddConstructor because this class has no default constructor.
        ;

    return tid;
}

void
Ipv4MpiFlowProbe::DoDispose()
{
    m_ipv4 = nullptr;
    m_classifier = nullptr;
    MpiFlowProbe::DoDispose();
}

void
Ipv4MpiFlowProbe::SendOutgoingLogger(
    const Ipv4Header& ipHeader,
    Ptr<const Packet> ipPayload,
    uint32_t interface)
{
    FlowId flowId;
    FlowPacketId packetId;

    if (!m_ipv4->IsUnicast(ipHeader.GetDestination()))
    {
        NS_ABORT_MSG("we are not prepared to handle broadcast yet");
    }

    Ipv4MpiFlowProbeTag fTag;
    bool found = ipPayload->FindFirstMatchingByteTag(fTag);
    if (found)
    {
        NS_ABORT_MSG("Found in sendOutgoing");
        return;
    }

    if (m_classifier->Classify(ipHeader, ipPayload, &flowId, &packetId))
    {
        uint32_t size = (ipPayload->GetSize() + ipHeader.GetSerializedSize());
        NS_LOG_DEBUG("ReportFirstTx (" << this << ", " << flowId << ", " << packetId << ", " << size
                                       << "); " << ipHeader << *ipPayload);
        m_flowMonitor->ReportFirstTx(this, flowId, packetId, size);

        // tag the packet with the flow id and packet id, so that the packet can be identified even
        // when Ipv4Header is not accessible at some non-IPv4 protocol layer
        Time now = Simulator::Now();
        Ipv4MpiFlowProbeTag fTag(flowId,
            packetId,
            size,
            ipHeader.GetSource(),
            ipHeader.GetDestination(),
            now.ToInteger(Time::GetResolution()),
            now.ToInteger(Time::GetResolution())
        );
        ipPayload->AddByteTag(fTag);
    }
}

void
Ipv4MpiFlowProbe::ForwardLogger(
    const Ipv4Header& ipHeader,
    Ptr<const Packet> ipPayload,
    uint32_t interface)
{
    NS_ABORT_MSG("Not implemented yet!");
}

void
Ipv4MpiFlowProbe::ForwardUpLogger(
    const Ipv4Header& ipHeader,
    Ptr<const Packet> ipPayload,
    uint32_t interface)
{   
    Ipv4MpiFlowProbeTag fTag;
    bool found = ipPayload->FindFirstMatchingByteTag(fTag);

    TcpHeader tcpHeader;
    ipPayload->PeekHeader(tcpHeader);

    if (tcpHeader.GetSourcePort() == Ipv4MpiFlowClassifier :: GetSourcePortToFilter())
        return;

    if (found)
    {
        if (!fTag.IsSrcDstValid(ipHeader.GetSource(), ipHeader.GetDestination()))
        {
            NS_LOG_INFO("Not reporting encapsulated packet");
            return;
        }

        FlowId flowId = fTag.GetFlowId();
        FlowPacketId packetId = fTag.GetPacketId();
        uint64_t tStart = fTag.GettTStart();
        uint64_t tLast = fTag.GetTLastRx();

        uint32_t size = (ipPayload->GetSize() + ipHeader.GetSerializedSize());
        NS_LOG_DEBUG("ReportLastRx (" << this << ", " << flowId << ", " << packetId << ", " << size
                                      << "); " << ipHeader << *ipPayload);
        m_flowMonitor->ReportLastRx(this, flowId, packetId, size, tStart, tLast);
    }
    else {
        NS_ABORT_MSG("Not found in forwardUp");
    }
}

void
Ipv4MpiFlowProbe::DropLogger(
    const Ipv4Header& ipHeader,
    Ptr<const Packet> ipPayload,
    Ipv4L3Protocol::DropReason reason,
    Ptr<Ipv4> ipv4,
    uint32_t ifIndex)
{
    Ipv4MpiFlowProbeTag fTag;
    bool found = ipPayload->FindFirstMatchingByteTag(fTag);

    if (found)
    {
        FlowId flowId = fTag.GetFlowId();
        FlowPacketId packetId = fTag.GetPacketId();

        uint32_t size = (ipPayload->GetSize() + ipHeader.GetSerializedSize());
        NS_LOG_INFO("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", "
                              << reason << ", destIp=" << ipHeader.GetDestination() << "); "
                              << "HDR: " << ipHeader << " PKT: " << *ipPayload);

        DropReason myReason;

        switch (reason)
        {
        case Ipv4L3Protocol::DROP_TTL_EXPIRED:
            myReason = DROP_TTL_EXPIRE;
            NS_LOG_DEBUG("DROP_TTL_EXPIRE");
            break;
        case Ipv4L3Protocol::DROP_NO_ROUTE:
            myReason = DROP_NO_ROUTE;
            NS_LOG_DEBUG("DROP_NO_ROUTE");
            break;
        case Ipv4L3Protocol::DROP_BAD_CHECKSUM:
            myReason = DROP_BAD_CHECKSUM;
            NS_LOG_DEBUG("DROP_BAD_CHECKSUM");
            break;
        case Ipv4L3Protocol::DROP_INTERFACE_DOWN:
            myReason = DROP_INTERFACE_DOWN;
            NS_LOG_DEBUG("DROP_INTERFACE_DOWN");
            break;
        case Ipv4L3Protocol::DROP_ROUTE_ERROR:
            myReason = DROP_ROUTE_ERROR;
            NS_LOG_DEBUG("DROP_ROUTE_ERROR");
            break;
        case Ipv4L3Protocol::DROP_FRAGMENT_TIMEOUT:
            myReason = DROP_FRAGMENT_TIMEOUT;
            NS_LOG_DEBUG("DROP_FRAGMENT_TIMEOUT");
            break;

        default:
            myReason = DROP_INVALID_REASON;
            NS_FATAL_ERROR("Unexpected drop reason code " << reason);
        }

        m_flowMonitor->ReportDrop(this, flowId, packetId, size, myReason);
    }
}

void
Ipv4MpiFlowProbe::QueueDropLogger(Ptr<const Packet> ipPayload)
{
    Ipv4MpiFlowProbeTag fTag;
    bool tagFound = ipPayload->FindFirstMatchingByteTag(fTag);

    if (!tagFound)
    {
        NS_LOG_INFO("Dropped a packet with no tag!");
        return;
    }

    FlowId flowId = fTag.GetFlowId();
    FlowPacketId packetId = fTag.GetPacketId();
    uint32_t size = fTag.GetPacketSize();

    NS_LOG_INFO("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", "
                          << DROP_QUEUE << "); ");

    m_flowMonitor->ReportDrop(this, flowId, packetId, size, DROP_QUEUE);
}

void
Ipv4MpiFlowProbe::QueueDiscDropLogger(Ptr<const QueueDiscItem> item)
{
    Ipv4MpiFlowProbeTag fTag;
    bool tagFound = item->GetPacket()->FindFirstMatchingByteTag(fTag);

    if (!tagFound)
    {
        return;
    }

    FlowId flowId = fTag.GetFlowId();
    FlowPacketId packetId = fTag.GetPacketId();
    uint32_t size = fTag.GetPacketSize();

    NS_LOG_INFO("Drop (" << this << ", " << flowId << ", " << packetId << ", " << size << ", "
                          << DROP_QUEUE_DISC << "); ");

    m_flowMonitor->ReportDrop(this, flowId, packetId, size, DROP_QUEUE_DISC);
}

} // namespace ns3
