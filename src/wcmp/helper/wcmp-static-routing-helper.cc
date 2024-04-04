#include "wcmp-static-routing-helper.h"
#include "ns3/ptr.h"
#include "ns3/node.h"

namespace ns3
{

WcmpStaticRoutingHelper :: WcmpStaticRoutingHelper() {
    this->m_factory.SetTypeId("ns3::wcmp::WcmpStaticRouting");
}

WcmpStaticRoutingHelper :: WcmpStaticRoutingHelper(const WcmpStaticRoutingHelper& o) 
    : m_factory(o.m_factory)
{
}

WcmpStaticRoutingHelper*
WcmpStaticRoutingHelper :: Copy() const {
    return new WcmpStaticRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
WcmpStaticRoutingHelper :: Create(Ptr<Node> node) const
{
    Ptr<wcmp::WcmpStaticRouting> agent = m_factory.Create<wcmp::WcmpStaticRouting>();
    node->AggregateObject(agent);
    return agent;
}

Ptr<wcmp::WcmpStaticRouting>
WcmpStaticRoutingHelper :: GetWcmpStaticRouting(Ptr<Ipv4> ipv4) const
{
    Ptr<Ipv4RoutingProtocol> ipv4rp = ipv4->GetRoutingProtocol();
    NS_ASSERT_MSG(ipv4rp, "No routing protocol associated with Ipv4");
    if (DynamicCast<wcmp::WcmpStaticRouting>(ipv4rp))
        return DynamicCast<wcmp::WcmpStaticRouting>(ipv4rp);

    if (DynamicCast<Ipv4ListRouting>(ipv4rp))
    {
        Ptr<Ipv4ListRouting> lrp = DynamicCast<Ipv4ListRouting>(ipv4rp);
        int16_t priority;
        for (uint32_t i = 0; i < lrp->GetNRoutingProtocols(); i++)
        {
            Ptr<Ipv4RoutingProtocol> temp = lrp->GetRoutingProtocol(i, priority);
            if (DynamicCast<wcmp::WcmpStaticRouting>(temp))
                return DynamicCast<wcmp::WcmpStaticRouting>(temp);
        }
    }
    
    return nullptr;
}

} // namespace ns3
