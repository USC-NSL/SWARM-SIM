#include "wcmp-static-routing.h"
#include "ns3/enum.h"
#include "ns3/node.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"

#include <iomanip>


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WcmpStaticRouting");

namespace wcmp
{

TypeId
WcmpStaticRouting :: GetTypeId() {
    static TypeId tid = 
        TypeId("ns3::wcmp::WcmpStaticRouting")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("wcmp")
            .AddConstructor<WcmpStaticRouting>()
            .AddAttribute(
                "HashAlg",
                "Hash what packet headers to do ECMP/WCMP",
                EnumValue<hash_alg_t>(hash_alg_t::HASH_IP_TCP_UDP),
                MakeEnumAccessor<hash_alg_t> (&WcmpStaticRouting::m_hash_alg),
                MakeEnumChecker(
                    hash_alg_t::HASH_IP_ONLY, "ip",
                    hash_alg_t::HASH_IP_TCP, "tcp",
                    hash_alg_t::HASH_IP_TCP_UDP, "tcpudp"
                )
            )
            .AddAttribute(
                "AddRouteOnUp",
                "Add a network route when an interface with an IP and mask comes up",
                BooleanValue(false),
                MakeBooleanAccessor(&WcmpStaticRouting::m_add_route_on_up),
                MakeBooleanChecker()
            );
    
    return tid;
}

WcmpStaticRouting :: WcmpStaticRouting() 
    : m_ipv4(nullptr)
{
    NS_LOG_FUNCTION(this);
}

WcmpStaticRouting :: ~WcmpStaticRouting() {
    NS_LOG_FUNCTION(this);
}

void
WcmpStaticRouting :: SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_ASSERT(!m_ipv4 && ipv4);
    this->m_ipv4 = ipv4;

    // This is where we initiate the WcmpWeigths object
    this->weights.set_ipv4(ipv4);

    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
    {
        if (m_ipv4->IsUp(i))
        {
            NotifyInterfaceUp(i);
        }
        else
        {
            NotifyInterfaceDown(i);
        }
    }
}

void
WcmpStaticRouting :: DoDispose() {
    for (auto route = this->m_networkRoutes.begin(); route != this->m_networkRoutes.end(); this->m_networkRoutes.erase(route)) {
        delete (route->first);
    }
    this->m_ipv4 = nullptr;
    Ipv4RoutingProtocol :: DoDispose();
}

void 
WcmpStaticRouting :: NotifyInterfaceUp(uint32_t i) {
    /**
     * We will not add a network route by default (this differs from the behavior
     * of NS-3 and Ipv4StaticRouting).
     * For debug and some other things, we optionally let that functionality be here,
     * but the only thing that we always do, is update the WcmpWeight object.
    */

    this->weights.set_state(i, true);
    if (this->m_add_route_on_up) {
        // TODO: Add route
    }
}

void
WcmpStaticRouting :: NotifyInterfaceDown(uint32_t i) {
    /**
     * We won't touch the static routes associated with this interface,
     * we just set its state.
     * If we were adding routes to this IP though, we should remove that.
    */

    this->weights.set_state(i, false);
    if (this->m_add_route_on_up) {
        // TODO: Remove the route
    }
}

void 
WcmpStaticRouting :: NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) {
    /**
     * Again, the only thing we need to is add a route if we have to
    */

    if (this->m_add_route_on_up) {
        // TODO: Add route!
    }
}

void
WcmpStaticRouting :: NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) {
    /**
     * For this, all routes going to this address need to go away
    */

    if (!this->m_ipv4->IsUp(interface)) {
        return;
    }

    Ipv4Address networkAddress = address.GetLocal().CombineMask(address.GetMask());
    Ipv4Mask networkMask = address.GetMask();
    for (auto it = this->m_networkRoutes.begin(); it != this->m_networkRoutes.end();) {
        if (
            it->first->GetInterface() == interface &&
            it->first->IsNetwork() &&
            it->first->GetDestNetwork() == networkAddress &&
            it->first->GetDestNetworkMask() == networkMask
        ) {
            // This route needs to go
            delete (it->first);
            this->m_networkRoutes.erase(it);
        }
        else {
            it++;
        }
    }
}

void
WcmpStaticRouting :: PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    std::ostream* os = stream->GetStream();
    // Copy the current ostream state
    std::ios oldState(nullptr);
    oldState.copyfmt(*os);

    *os << std::resetiosflags(std::ios::adjustfield) << std::setiosflags(std::ios::left);

    *os << "Node: " << m_ipv4->GetObject<Node>()->GetId() << ", Time: " << Now().As(unit)
        << ", Local time: " << m_ipv4->GetObject<Node>()->GetLocalTime().As(unit)
        << ", Ipv4StaticRouting table" << std::endl;

    // Implement this

    *os << std::endl;
    // Restore the previous ostream state
    (*os).copyfmt(oldState);
}



} // namespace wcmp
} // namespace ns3
