#ifndef MPI_FLOW_MONITOR_H
#define MPI_FLOW_MONITOR_H

#include "mpi-flow-probe.h"
#include "mpi-flow-classifier.h"

#include "ns3/event-id.h"
#include "ns3/histogram.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <map>
#include <vector>

namespace ns3
{

class MpiFlowMonitor : public Object
{
  public:
    struct FlowStats
    {   
        Time timeFirstTxPacket;
        Time timeFirstRxPacket;
        Time timeLastTxPacket;
        Time timeLastRxPacket;

        uint64_t txBytes;
        uint64_t rxBytes;

        std::vector<uint32_t>
            packetsDropped; // packetsDropped[reasonCode] => number of dropped packets

        std::vector<uint64_t> bytesDropped;   // bytesDropped[reasonCode] => number of dropped bytes
    };

    // --- basic methods ---
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    MpiFlowMonitor();

    void AddFlowClassifier(Ptr<MpiFlowClassifier> classifier);

    void SetSystemId(uint32_t systemId) {
        m_systemId = systemId;
    }

    void Start(const Time& time);
    void Stop(const Time& time);
    void StartRightNow();
    void StopRightNow();

    void AddProbe(Ptr<MpiFlowProbe> probe);

    void ReportFirstTx(
        Ptr<MpiFlowProbe> probe,
        FlowId flowId,
        FlowPacketId packetId,
        uint32_t packetSize);

    void ReportForwarding(
        Ptr<MpiFlowProbe> probe,
        FlowId flowId,
        FlowPacketId packetId,
        uint32_t packetSize);

    void ReportForwarding(
        Ptr<MpiFlowProbe> probe,
        FlowId flowId,
        FlowPacketId packetId,
        uint32_t packetSize,
        uint64_t tStart,
        uint64_t tLastRx);

    void ReportLastRx(
        Ptr<MpiFlowProbe> probe,
        FlowId flowId,
        FlowPacketId packetId,
        uint32_t packetSize);

    void ReportLastRx(
        Ptr<MpiFlowProbe> probe,
        FlowId flowId,
        FlowPacketId packetId,
        uint32_t packetSize,
        uint64_t tStart,
        uint64_t tLastRx);

    void ReportDrop(
        Ptr<MpiFlowProbe> probe,
        FlowId flowId,
        FlowPacketId packetId,
        uint32_t packetSize,
        uint32_t reasonCode); 

    // --- methods to get the results ---

    typedef std::map<FlowId, FlowStats> FlowStatsContainer;
    typedef std::map<FlowId, FlowStats>::iterator FlowStatsContainerI;
    typedef std::map<FlowId, FlowStats>::const_iterator FlowStatsContainerCI;
    typedef std::vector<Ptr<MpiFlowProbe>> FlowProbeContainer;
    typedef std::vector<Ptr<MpiFlowProbe>>::iterator FlowProbeContainerI;
    typedef std::vector<Ptr<MpiFlowProbe>>::const_iterator FlowProbeContainerCI;

    const FlowStatsContainer& GetFlowStats() const;
    const FlowProbeContainer& GetAllProbes() const;
    void SerializeToXmlStream(
        std::ostream& os,
        uint16_t indent,
        bool enableHistograms,
        bool enableProbes
    );

    std::string SerializeToXmlString(uint16_t indent, bool enableHistograms, bool enableProbes);
    void SerializeToXmlFile(std::string fileName, bool enableHistograms, bool enableProbes);

    /// Reset all the statistics
    void ResetAllStats();

  protected:
    void NotifyConstructionCompleted() override;
    void DoDispose() override;

  private:
    /// Structure to represent a single tracked packet data
    struct TrackedPacket
    {
        Time firstSeenTime;      //!< absolute time when the packet was first seen by a probe
        Time lastSeenTime;       //!< absolute time when the packet was last seen by a probe
    };

    /// FlowId --> FlowStats
    FlowStatsContainer m_flowStats;

    /// (FlowId,PacketId) --> TrackedPacket
    typedef std::map<std::pair<FlowId, FlowPacketId>, TrackedPacket> TrackedPacketMap;
    TrackedPacketMap m_trackedPackets; //!< Tracked packets
    Time m_maxPerHopDelay;             //!< Minimum per-hop delay
    FlowProbeContainer m_flowProbes;   //!< all the FlowProbes

    // note: this is needed only for serialization
    std::list<Ptr<MpiFlowClassifier>> m_classifiers; //!< the FlowClassifiers

    EventId m_startEvent;               //!< Start event
    EventId m_stopEvent;                //!< Stop event
    bool m_enabled;                     //!< FlowMon is enabled
    Time m_flowInterruptionsMinTime;    //!< Flow interruptions minimum time
    uint32_t m_systemId;

    FlowStats& GetStatsForFlow(FlowId flowId);
};

} // namespace ns3

#endif /* MPI_FLOW_MONITOR_H */
