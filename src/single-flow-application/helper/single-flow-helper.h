#ifndef SINGLE_FLOW_HELPER_H
#define SINGLE_FLOW_HELPER_H

#include "ns3/address.h"
#include "ns3/application-container.h"
#include "ns3/attribute.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/single-flow-application.h"

#include <stdint.h>
#include <string>

namespace ns3
{
  class SingleFlowHelper
  {
    public:
      SingleFlowHelper(std::string protocol);
      SingleFlowHelper(std::string protocol, Address address);

      void SetAttribute(std::string name, const AttributeValue& value);

      ApplicationContainer Install(NodeContainer c) const;

      ApplicationContainer Install(Ptr<Node> node) const;

      ApplicationContainer Install(std::string nodeName) const;

    private:
      Ptr<Application> InstallPriv(Ptr<Node> node) const;

      ObjectFactory m_factory; //!< Object factory.
  };
} // namespace ns3

#endif /* SINGLE_FLOW_HELPER_H */
