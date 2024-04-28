#ifndef IPV4_MPI_FLOW_CLASSIFIER_H
#define IPV4_MPI_FLOW_CLASSIFIER_H

#include "mpi-flow-classifier.h"

#include "ns3/ipv4-header.h"

#include <map>
#include <stdint.h>

namespace ns3
{

class Packet;

class Ipv4MpiFlowClassifier : public MpiFlowClassifier
{
  public:
    /// Structure to classify a packet
    struct FiveTuple
    {
        Ipv4Address sourceAddress;      //!< Source address
        Ipv4Address destinationAddress; //!< Destination address
        uint8_t protocol;               //!< Protocol
        uint16_t sourcePort;            //!< Source port
        uint16_t destinationPort;       //!< Destination port
    };

    Ipv4MpiFlowClassifier();

    bool Classify(const Ipv4Header& ipHeader,
                  Ptr<const Packet> ipPayload,
                  uint32_t* out_flowId,
                  uint32_t* out_packetId);

    FiveTuple FindFlow(FlowId flowId) const;

    /// Comparator used to sort the vector of DSCP values
    class SortByCount
    {
      public:
        bool operator()(std::pair<Ipv4Header::DscpType, uint32_t> left,
                        std::pair<Ipv4Header::DscpType, uint32_t> right);
    };

    std::vector<std::pair<Ipv4Header::DscpType, uint32_t>> GetDscpCounts(FlowId flowId) const;

    void SerializeToXmlStream(std::ostream& os, uint16_t indent) const override;

  private:
    /// Map to Flows Identifiers to FlowIds
    std::map<FiveTuple, FlowId> m_flowMap;
    /// Map to FlowIds to FlowPacketId
    std::map<FlowId, FlowPacketId> m_flowPktIdMap;
    /// Map FlowIds to (DSCP value, packet count) pairs
    std::map<FlowId, std::map<Ipv4Header::DscpType, uint32_t>> m_flowDscpMap;
};

/**
 * \brief Less than operator.
 *
 * \param t1 the first operand
 * \param t2 the first operand
 * \returns true if the operands are equal
 */
bool operator<(const Ipv4MpiFlowClassifier::FiveTuple& t1, const Ipv4MpiFlowClassifier::FiveTuple& t2);

/**
 * \brief Equal to operator.
 *
 * \param t1 the first operand
 * \param t2 the first operand
 * \returns true if the operands are equal
 */
bool operator==(const Ipv4MpiFlowClassifier::FiveTuple& t1, const Ipv4MpiFlowClassifier::FiveTuple& t2);

} // namespace ns3

#endif /* IPV4_MPI_FLOW_CLASSIFIER_H */
