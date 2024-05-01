#include "single-flow-helper.h"

#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/names.h"
#include "ns3/onoff-application.h"
#include "ns3/packet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

namespace ns3
{
    SingleFlowHelper::SingleFlowHelper(std::string protocol, Address address)
    {
        m_factory.SetTypeId("ns3::SingleFlowApplication");
        m_factory.Set("Protocol", StringValue(protocol));
        m_factory.Set("Remote", AddressValue(address));
    }

    void
    SingleFlowHelper::SetAttribute(std::string name, const AttributeValue& value)
    {
        m_factory.Set(name, value);
    }

    ApplicationContainer
    SingleFlowHelper::Install(Ptr<Node> node) const
    {
        return ApplicationContainer(InstallPriv(node));
    }

    ApplicationContainer
    SingleFlowHelper::Install(std::string nodeName) const
    {
        Ptr<Node> node = Names::Find<Node>(nodeName);
        return ApplicationContainer(InstallPriv(node));
    }

    ApplicationContainer
    SingleFlowHelper::Install(NodeContainer c) const
    {
        ApplicationContainer apps;
        for (auto i = c.Begin(); i != c.End(); ++i)
        {
            apps.Add(InstallPriv(*i));
        }

        return apps;
    }

    Ptr<Application>
    SingleFlowHelper::InstallPriv(Ptr<Node> node) const
    {
        Ptr<Application> app = m_factory.Create<Application>();
        node->AddApplication(app);

        return app;
    }
} // namespace ns3
