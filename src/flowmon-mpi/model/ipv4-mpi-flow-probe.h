#ifndef IPV4_MPI_FLOW_PROBE_H
#define IPV4_MPI_FLOW_PROBE_H

#include "mpi-flow-probe.h"
#include "ipv4-mpi-flow-classifier.h"

#include "ns3/ipv4-l3-protocol.h"
#include "ns3/queue-item.h"

namespace ns3
{

class MpiFlowMonitor;
class Node;

class Ipv4MpiFlowProbeTag : public Tag
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer buf) const override;
    void Deserialize(TagBuffer buf) override;
    void Print(std::ostream& os) const override;
    Ipv4MpiFlowProbeTag();
    
    Ipv4MpiFlowProbeTag(uint32_t flowId,
                     uint32_t packetId,
                     uint32_t packetSize,
                     Ipv4Address src,
                     Ipv4Address dst,
                     
                     uint64_t tStart,
                     uint64_t tLasttRx
                     
                     );
    void SetFlowId(uint32_t flowId);
    void SetPacketId(uint32_t packetId);
    void SetPacketSize(uint32_t packetSize);
    void SettTStart(uint64_t tStart);
    void SetTLastRx(uint64_t tLastRx);

    uint32_t GetFlowId() const;
    uint32_t GetPacketId() const;
    uint32_t GetPacketSize() const;
    uint64_t GettTStart() const;
    uint64_t GetTLastRx() const;
    bool IsSrcDstValid(Ipv4Address src, Ipv4Address dst) const;

  private:
    uint32_t m_flowId;     //!< flow identifier
    uint32_t m_packetId;   //!< packet identifier
    uint32_t m_packetSize; //!< packet size
    Ipv4Address m_src;     //!< IP source
    Ipv4Address m_dst;     //!< IP destination
    uint64_t m_tStart;     //!< Timestamp of first time seeing this packet
    uint64_t m_tLastRx;    //!< Timestamp of last time receiving this packet
};

class Ipv4MpiFlowProbe : public MpiFlowProbe
{
  public:
    Ipv4MpiFlowProbe(Ptr<MpiFlowMonitor> monitor, Ptr<Ipv4MpiFlowClassifier> classifier, Ptr<Node> node);
    ~Ipv4MpiFlowProbe() override;

    static TypeId GetTypeId();

    enum DropReason
    {
        /// Packet dropped due to missing route to the destination
        DROP_NO_ROUTE = 0,

        /// Packet dropped due to TTL decremented to zero during IPv4 forwarding
        DROP_TTL_EXPIRE,

        /// Packet dropped due to invalid checksum in the IPv4 header
        DROP_BAD_CHECKSUM,

        /// Packet dropped due to queue overflow.  Note: only works for
        /// NetDevices that provide a TxQueue attribute of type Queue
        /// with a Drop trace source.  It currently works with Csma and
        /// PointToPoint devices, but not with WiFi or WiMax.
        DROP_QUEUE,

        /// Packet dropped by the queue disc
        DROP_QUEUE_DISC,

        DROP_INTERFACE_DOWN,   /**< Interface is down so can not send packet */
        DROP_ROUTE_ERROR,      /**< Route error */
        DROP_FRAGMENT_TIMEOUT, /**< Fragment timeout exceeded */

        DROP_INVALID_REASON, /**< Fallback reason (no known reason) */
    };

  protected:
    void DoDispose() override;

  private:
    void SendOutgoingLogger(const Ipv4Header& ipHeader,
                            Ptr<const Packet> ipPayload,
                            uint32_t interface);
    void ForwardLogger(const Ipv4Header& ipHeader, Ptr<const Packet> ipPayload, uint32_t interface);
    void ForwardUpLogger(const Ipv4Header& ipHeader,
                         Ptr<const Packet> ipPayload,
                         uint32_t interface);
    void DropLogger(const Ipv4Header& ipHeader,
                    Ptr<const Packet> ipPayload,
                    Ipv4L3Protocol::DropReason reason,
                    Ptr<Ipv4> ipv4,
                    uint32_t ifIndex);
    void QueueDropLogger(Ptr<const Packet> ipPayload);
    void QueueDiscDropLogger(Ptr<const QueueDiscItem> item);

    Ptr<Ipv4MpiFlowClassifier> m_classifier; //!< the Ipv4MpiFlowClassifier this probe is associated with
    Ptr<Ipv4L3Protocol> m_ipv4;              //!< the Ipv4L3Protocol this probe is bound to
};

} // namespace ns3

#endif /* IPV4_MPI_FLOW_PROBE_H */
