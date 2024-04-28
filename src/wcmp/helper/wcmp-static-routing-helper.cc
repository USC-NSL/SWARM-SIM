#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/integer.h"
#include "ns3/boolean.h"
#include "wcmp-static-routing-helper.h"

namespace ns3
{

WcmpStaticRoutingHelper :: WcmpStaticRoutingHelper() {
    this->m_factory.SetTypeId("ns3::wcmp::WcmpStaticRouting");
}

WcmpStaticRoutingHelper :: WcmpStaticRoutingHelper(uint16_t level, level_mapper_func f) {
    this->m_func = f;
    this->m_factory.SetTypeId("ns3::wcmp::WcmpStaticRouting");
    this->m_factory.Set("level", IntegerValue(level));
}


WcmpStaticRoutingHelper :: WcmpStaticRoutingHelper(const WcmpStaticRoutingHelper& o) 
    : m_factory(o.m_factory)
{
}

void
WcmpStaticRoutingHelper :: doEcmp() {
    this->m_factory.Set("ecmp", BooleanValue(true));
}

void
WcmpStaticRoutingHelper :: useCache() {
    this->m_factory.Set("UseCache", BooleanValue(true));
}

WcmpStaticRoutingHelper*
WcmpStaticRoutingHelper :: Copy() const {
    return new WcmpStaticRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
WcmpStaticRoutingHelper :: Create(Ptr<Node> node) const
{
    Ptr<wcmp::WcmpStaticRouting> agent = m_factory.Create<wcmp::WcmpStaticRouting>();
    agent->SetMapperFunction(this->m_func);
    node->AggregateObject(agent);
    return agent;
}

Ptr<wcmp::WcmpStaticRouting>
WcmpStaticRoutingHelper :: GetWcmpStaticRouting(Ptr<Ipv4> ipv4) const
{
    Ptr<Ipv4RoutingProtocol> ipv4rp = ipv4->GetRoutingProtocol();
    NS_ASSERT_MSG(ipv4rp, "No routing protocol associated with Ipv4");

    // Check the main routing protocol
    Ptr<wcmp::WcmpStaticRouting> wcmp = DynamicCast<wcmp::WcmpStaticRouting>(ipv4rp);

    if (wcmp)
        return wcmp;

    // Check the list routing protocols
    Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(ipv4rp);

    if (list)
    {
        int16_t priority;
        Ptr<Ipv4RoutingProtocol> lrp;
        
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); i++)
        {
            lrp = list->GetRoutingProtocol(i, priority);
            wcmp = DynamicCast<wcmp::WcmpStaticRouting>(lrp);
            if (wcmp)
                return wcmp;
        }
    }
    
    return nullptr;
}

void 
WcmpStaticRoutingHelper :: SetInterfaceWeight(Ptr<Ipv4> ipv4, uint32_t interface, uint16_t level, uint16_t weight) {
    GetWcmpStaticRouting(ipv4)->SetInterfaceWeight(interface, level, weight);
}

} // namespace ns3
