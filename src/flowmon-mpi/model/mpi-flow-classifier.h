#ifndef MPI_FLOW_CLASSIFIER_H
#define MPI_FLOW_CLASSIFIER_H

#include "ns3/simple-ref-count.h"

#include <ostream>

namespace ns3
{

typedef uint32_t FlowId;

typedef uint32_t FlowPacketId;

class MpiFlowClassifier : public SimpleRefCount<MpiFlowClassifier>
{
  private:
    FlowId m_lastNewFlowId; //!< Last known Flow ID
    uint32_t m_systemId;

  public:
    MpiFlowClassifier();
    virtual ~MpiFlowClassifier();

    // Delete copy constructor and assignment operator to avoid misuse
    MpiFlowClassifier(const MpiFlowClassifier&) = delete;
    MpiFlowClassifier& operator=(const MpiFlowClassifier&) = delete;

    virtual void SerializeToXmlStream(std::ostream& os, uint16_t indent) const = 0;

    void SetSystemId(uint32_t systemId) {
      m_systemId = systemId;
      m_lastNewFlowId = systemId << 26;
    }

  protected:
    FlowId GetNewFlowId();

    void Indent(std::ostream& os, uint16_t level) const;
};

inline void
MpiFlowClassifier::Indent(std::ostream& os, uint16_t level) const
{
    for (uint16_t __xpto = 0; __xpto < level; __xpto++)
    {
        os << ' ';
    }
}

} // namespace ns3

#endif /* MPI_FLOW_CLASSIFIER_H */
