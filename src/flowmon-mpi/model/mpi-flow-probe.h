#ifndef MPI_FLOW_PROBE_H
#define MPI_FLOW_PROBE_H

#include "mpi-flow-classifier.h"

#include "ns3/nstime.h"
#include "ns3/object.h"

#include <map>
#include <vector>

namespace ns3
{

class MpiFlowMonitor;

class MpiFlowProbe : public Object
{
  protected:
    MpiFlowProbe(Ptr<MpiFlowMonitor> flowMonitor);
    void DoDispose() override;

  public:
    ~MpiFlowProbe() override;

    MpiFlowProbe(const MpiFlowProbe&) = delete;
    MpiFlowProbe& operator=(const MpiFlowProbe&) = delete;

    /// Register this type.
    /// \return The TypeId.
    static TypeId GetTypeId();

    /// Structure to hold the statistics of a flow
    struct FlowStats
    {
        FlowStats()
            : delayFromFirstProbeSum(Seconds(0)),
              bytes(0),
              packets(0)
        {
        }

        /// packetsDropped[reasonCode] => number of dropped packets
        std::vector<uint32_t> packetsDropped;
        /// bytesDropped[reasonCode] => number of dropped bytes
        std::vector<uint64_t> bytesDropped;
        /// divide by 'packets' to get the average delay from the
        /// first (entry) probe up to this one (partial delay)
        Time delayFromFirstProbeSum;
        /// Number of bytes seen of this flow
        uint64_t bytes;
        /// Number of packets seen of this flow
        uint32_t packets;
    };

    /// Container to map FlowId -> FlowStats
    typedef std::map<FlowId, FlowStats> Stats;

    void AddPacketStats(FlowId flowId, uint32_t packetSize, Time delayFromFirstProbe);
    void AddPacketDropStats(FlowId flowId, uint32_t packetSize, uint32_t reasonCode);

    Stats GetStats() const;
    
    void SerializeToXmlStream(std::ostream& os, uint16_t indent, uint32_t index) const;

  protected:
    Ptr<MpiFlowMonitor> m_flowMonitor; //!< the FlowMonitor instance
    Stats m_stats;                  //!< The flow stats
};

} // namespace ns3

#endif /* MPI_FLOW_PROBE_H */
