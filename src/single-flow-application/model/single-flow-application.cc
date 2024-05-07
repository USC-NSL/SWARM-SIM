#include "single-flow-application.h"

#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/packet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/object-vector.h"

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("SingleFlowApplication");

NS_OBJECT_ENSURE_REGISTERED(SingleFlowApplication);

TypeId
SingleFlowApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SingleFlowApplication")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<SingleFlowApplication>()
            .AddAttribute("DataRate",
                        "The data rate in on state.",
                        DataRateValue(DataRate("500kb/s")),
                        MakeDataRateAccessor(&SingleFlowApplication::m_cbrRate),
                        MakeDataRateChecker())
            .AddAttribute("PacketSize",
                        "The size of packets sent in on state",
                        UintegerValue(512),
                        MakeUintegerAccessor(&SingleFlowApplication::m_pktSize),
                        MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("Remote",
                        "The address of the destination",
                        AddressValue(),
                        MakeAddressAccessor(&SingleFlowApplication::m_peer),
                        MakeAddressChecker())
            .AddAttribute("Local",
                        "The Address on which to bind the socket. If not set, it is generated "
                        "automatically.",
                        AddressValue(),
                        MakeAddressAccessor(&SingleFlowApplication::m_local),
                        MakeAddressChecker())
            .AddAttribute("FlowSize",
                        "Size of this flow. Once the flow is complete, the application "
                        "will self terminate and return.",
                        UintegerValue(1024),
                        MakeUintegerAccessor(&SingleFlowApplication::m_flowSize),
                        MakeUintegerChecker<uint64_t>())
            .AddAttribute("Protocol",
                        "The type of protocol to use. This should be "
                        "a subclass of ns3::SocketFactory",
                        TypeIdValue(UdpSocketFactory::GetTypeId()),
                        MakeTypeIdAccessor(&SingleFlowApplication::m_tid),
                        // This should check for SocketFactory as a parent
                        MakeTypeIdChecker())
            .AddTraceSource("Tx",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&SingleFlowApplication::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("TxWithAddresses",
                            "A new packet is created and is sent",
                            MakeTraceSourceAccessor(&SingleFlowApplication::m_txTraceWithAddresses),
                            "ns3::Packet::TwoAddressTracedCallback");
    return tid;
}

SingleFlowApplication::SingleFlowApplication()
    : m_socket(nullptr),
    m_connected(false),
    m_residualBits(0),
    m_lastStartTime(Seconds(0)),
    m_totBytes(0),
    m_unsentPacket(nullptr)
{
    NS_LOG_FUNCTION(this);
}

SingleFlowApplication::~SingleFlowApplication()
{
    NS_LOG_FUNCTION(this);
}

void
SingleFlowApplication::SetFlowSize(uint64_t flowSize)
{
    NS_LOG_FUNCTION(this << flowSize);
    m_flowSize = flowSize;
}

void 
SingleFlowApplication::SetNode(Ptr<Node> node) {
    NS_LOG_FUNCTION(this);
    m_node = node;
}

void 
SingleFlowApplication::SetAppId(uint32_t appid) {
    NS_LOG_FUNCTION(this);
    m_appId = appid;
}

Ptr<Socket>
SingleFlowApplication::GetSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}

void
SingleFlowApplication::DoDispose()
{
    NS_LOG_FUNCTION(this);

    CancelEvents();
    m_socket = nullptr;
    m_unsentPacket = nullptr;
    
    // chain up
    Application::DoDispose();

    // TODO: Figure out how to do this
    // Remove yourself from the application list
    // ObjectVectorValue val;
    // m_node->GetAttribute("ApplicationList", val);
}

// Application Methods
void
SingleFlowApplication::StartApplication() // Called at time specified by Start
{
    NS_LOG_FUNCTION(this);

    // Create the socket if not already
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);
        int ret = -1;

        if (!m_local.IsInvalid())
        {
            NS_ABORT_MSG_IF((Inet6SocketAddress::IsMatchingType(m_peer) &&
                            InetSocketAddress::IsMatchingType(m_local)) ||
                                (InetSocketAddress::IsMatchingType(m_peer) &&
                                Inet6SocketAddress::IsMatchingType(m_local)),
                            "Incompatible peer and local address IP version");
            ret = m_socket->Bind(m_local);
        }
        else
        {
            if (Inet6SocketAddress::IsMatchingType(m_peer))
            {
                ret = m_socket->Bind6();
            }
            else if (InetSocketAddress::IsMatchingType(m_peer) ||
                    PacketSocketAddress::IsMatchingType(m_peer))
            {
                ret = m_socket->Bind();
            }
        }

        if (ret == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }

        m_socket->SetConnectCallback(MakeCallback(&SingleFlowApplication::ConnectionSucceeded, this),
                                    MakeCallback(&SingleFlowApplication::ConnectionFailed, this));

        m_socket->Connect(m_peer);

        // We will not allow broadcast for these flows
        m_socket->SetAllowBroadcast(false);
        m_socket->ShutdownRecv();
    }
    m_cbrRateFailSafe = m_cbrRate;

    // Ensure no pending event
    CancelEvents();

    // If we are not yet connected, there is nothing to do here,
    // the ConnectionComplete upcall will start timers at that time.
    // If we are already connected, CancelEvents did remove the events,
    // so we have to start them again.
    if (m_connected)
    {
        ScheduleStartEvent();
    }
}

void
SingleFlowApplication::StopApplication() // Called at time specified by Stop
{
    NS_LOG_FUNCTION(this);

    CancelEvents();
    if (m_socket)
    {
        m_socket->Close();
    }
    else
    {
        NS_LOG_WARN("SingleFlowApplication found null socket to close in StopApplication");
    }

    // Out single FlowApp self-disposes when it is finished
    if (!m_reportDone) {
        DoDispose();
    }
    else {
        m_isDone = true;
    }
}

void
SingleFlowApplication::CancelEvents()
{
    NS_LOG_FUNCTION(this);

    if (m_sendEvent.IsRunning() && m_cbrRateFailSafe == m_cbrRate)
    {
        Time delta(Simulator::Now() - m_lastStartTime);
        int64x64_t bits = delta.To(Time::S) * m_cbrRate.GetBitRate();
        m_residualBits += bits.GetHigh();
    }
    m_cbrRateFailSafe = m_cbrRate;
    Simulator::Cancel(m_sendEvent);
    Simulator::Cancel(m_startStopEvent);
    if (m_unsentPacket)
    {
        NS_LOG_DEBUG("Discarding cached packet upon CancelEvents ()");
    }
    m_unsentPacket = nullptr;
}

// Event handlers
void
SingleFlowApplication::StartSending()
{
    NS_LOG_FUNCTION(this);
    m_lastStartTime = Simulator::Now();
    ScheduleNextTx(); // Schedule the send packet event
}

void
SingleFlowApplication::StopSending()
{
    NS_LOG_FUNCTION(this);
    CancelEvents();

    ScheduleStartEvent();
}

// Private helpers
void
SingleFlowApplication::ScheduleNextTx()
{
    NS_LOG_FUNCTION(this);

    if (m_flowSize == 0 || m_totBytes < m_flowSize)
    {
        NS_ABORT_MSG_IF(m_residualBits > m_pktSize * 8,
                        "Calculation to compute next send time will overflow");
        uint32_t bits = m_pktSize * 8 - m_residualBits;
        NS_LOG_LOGIC("bits = " << bits);
        Time nextTime(
            Seconds(bits / static_cast<double>(m_cbrRate.GetBitRate()))); // Time till next packet
        NS_LOG_LOGIC("nextTime = " << nextTime.As(Time::S));
        m_sendEvent = Simulator::Schedule(nextTime, &SingleFlowApplication::SendPacket, this);
    }
    else
    { // All done, cancel any pending events
        StopApplication();
    }
}

void
SingleFlowApplication::ScheduleStartEvent()
{
    NS_LOG_FUNCTION(this);
    // The single flow applications sends packets with no pause
    m_startStopEvent = Simulator::Schedule(Seconds(0), &SingleFlowApplication::StartSending, this);
}

void
SingleFlowApplication::SendPacket()
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT(m_sendEvent.IsExpired());

    Ptr<Packet> packet;
    if (m_unsentPacket)
    {
        packet = m_unsentPacket;
    }
    else
    {
        packet = Create<Packet>(m_pktSize);
    }

    int actual = m_socket->Send(packet);
    if ((unsigned)actual == m_pktSize)
    {
        m_txTrace(packet);
        m_totBytes += m_pktSize;
        m_unsentPacket = nullptr;
        Address localAddress;
        m_socket->GetSockName(localAddress);
        if (InetSocketAddress::IsMatchingType(m_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                << packet->GetSize() << " bytes to "
                                << InetSocketAddress::ConvertFrom(m_peer).GetIpv4() << " port "
                                << InetSocketAddress::ConvertFrom(m_peer).GetPort()
                                << " total Tx " << m_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, InetSocketAddress::ConvertFrom(m_peer));
        }
        else if (Inet6SocketAddress::IsMatchingType(m_peer))
        {
            NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " on-off application sent "
                                << packet->GetSize() << " bytes to "
                                << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6() << " port "
                                << Inet6SocketAddress::ConvertFrom(m_peer).GetPort()
                                << " total Tx " << m_totBytes << " bytes");
            m_txTraceWithAddresses(packet, localAddress, Inet6SocketAddress::ConvertFrom(m_peer));
        }
    }
    else
    {
        NS_LOG_DEBUG("Unable to send packet; actual " << actual << " size " << m_pktSize
                                                    << "; caching for later attempt");
        m_unsentPacket = packet;
    }
    m_residualBits = 0;
    m_lastStartTime = Simulator::Now();
    ScheduleNextTx();
}

void
SingleFlowApplication::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    ScheduleStartEvent();
    m_connected = true;
}

void
SingleFlowApplication::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    NS_FATAL_ERROR("Can't connect");
}

} // namespace ns3
