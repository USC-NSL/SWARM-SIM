#include "mpi-flow-classifier.h"

namespace ns3
{

MpiFlowClassifier::MpiFlowClassifier()
    : m_lastNewFlowId(0)
{
}

MpiFlowClassifier::~MpiFlowClassifier()
{
}

FlowId
MpiFlowClassifier::GetNewFlowId()
{
    return ++m_lastNewFlowId;
}

} // namespace ns3
