#ifndef MPI_FLOW_MONITOR_HELPER_H
#define MPI_FLOW_MONITOR_HELPER_H

#include "ns3/mpi-flow-classifier.h"
#include "ns3/mpi-flow-monitor.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/ipv4-mpi-flow-probe.h"

#include <string>

namespace ns3
{

class AttributeValue;

class MpiFlowMonitorHelper
{
  public:
    static uint32_t m_systemId;
    static uint16_t sourcePortToFilter;

    MpiFlowMonitorHelper();
    ~MpiFlowMonitorHelper();

    // Delete copy constructor and assignment operator to avoid misuse
    MpiFlowMonitorHelper(const MpiFlowMonitorHelper&) = delete;
    MpiFlowMonitorHelper& operator=(const MpiFlowMonitorHelper&) = delete;

    void SetMonitorAttribute(std::string n1, const AttributeValue& v1);

    Ptr<MpiFlowMonitor> Install(NodeContainer nodes);
    Ptr<MpiFlowMonitor> Install(Ptr<Node> node);
    Ptr<MpiFlowMonitor> InstallAll();
    Ptr<MpiFlowMonitor> GetMonitor();
    Ptr<MpiFlowClassifier> GetClassifier();

    void SerializeToXmlStream(
        std::ostream& os,
        uint16_t indent,
        bool enableHistograms,
        bool enableProbes);

    std::string SerializeToXmlString(uint16_t indent, bool enableHistograms, bool enableProbes);

    void SerializeToXmlFile(std::string fileName, bool enableHistograms, bool enableProbes);

    static void SetSystemId(uint32_t systemId) {
      MpiFlowMonitorHelper :: m_systemId = systemId;
    }

    static void SetSourcePortToFilter(uint16_t port) {
      MpiFlowMonitorHelper :: sourcePortToFilter = port;
      Ipv4MpiFlowClassifier :: SetSourcePortToFilter(port);
    }

    static void SetMonitorUntil(double when) {
      Ipv4MpiFlowClassifier :: SetMonitorUntil(when);
    }

    uint32_t GetSystemId() {
      return MpiFlowMonitorHelper :: m_systemId;
    }

    uint16_t GetSourcePortToFilter() {
      return MpiFlowMonitorHelper :: sourcePortToFilter;
    }

  private:
    ObjectFactory m_monitorFactory;        //!< Object factory
    Ptr<MpiFlowMonitor> m_flowMonitor;        //!< the FlowMonitor object
    Ptr<MpiFlowClassifier> m_flowClassifier4; //!< the MpiFlowClassifier object for IPv4
};

uint16_t MpiFlowMonitorHelper :: sourcePortToFilter = 0;

uint32_t MpiFlowMonitorHelper :: m_systemId;

} // namespace ns3

#endif /* MPI_FLOW_MONITOR_HELPER_H */
