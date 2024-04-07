#include "wcmp-static-routing.h"
#include "ns3/enum.h"
#include "ns3/names.h"
#include "ns3/node.h"
#include "ns3/boolean.h"
#include "ns3/simulator.h"

#include <iomanip>


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WcmpStaticRouting");

namespace wcmp
{

NS_OBJECT_ENSURE_REGISTERED(WcmpStaticRouting);

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
    for (auto route = this->m_networkRoutes.begin(); route != this->m_networkRoutes.end(); route = this->m_networkRoutes.erase(route)) {
        delete (route->first);
    }
    this->m_ipv4 = nullptr;
    Ipv4RoutingProtocol :: DoDispose();
}

std::vector<Ipv4RoutingTableEntry*> 
WcmpStaticRouting :: MultiLpm(Ipv4Address dest) {
    std::vector<Ipv4RoutingTableEntry*> entries;
    uint16_t longest_mask = 0;
    uint32_t shortest_metric = 0xffffffff;

    for (auto i = m_networkRoutes.begin(); i != m_networkRoutes.end(); i++)
    {
        Ipv4RoutingTableEntry* j = i->first;
        uint32_t metric = i->second;
        Ipv4Mask mask = (j)->GetDestNetworkMask();
        uint16_t masklen = mask.GetPrefixLength();
        Ipv4Address entry = (j)->GetDestNetwork();

        NS_LOG_LOGIC("LPM check for entry " << entry << "/" << (j)->GetDestNetworkMask() 
        << " (" << metric << ")" << " --> " << j->GetInterface() << " against " << dest);

        if (mask.IsMatch(dest, entry))
        {
            // Found a route ...
            NS_LOG_LOGIC("Found route");
            if (masklen < longest_mask)
            {
                // Short match
                NS_LOG_LOGIC("Short match");
                continue;
            }
            else if (masklen > longest_mask)
            {
                // Reset metric if longer masklen
                shortest_metric = 0xffffffff;
            }
            
            longest_mask = masklen;

            if (metric > shortest_metric)
            {
                // Metric is larger, skip
                NS_LOG_LOGIC("Metric big");
                continue;
            }
            else if (metric < shortest_metric) {
                // Better path, clear the collected entries
                entries.clear();
            }

            shortest_metric = metric;

            entries.push_back(j);
        }
    }
    
    /**
     * Some of the entries here might be bound to a down interface.
     * During lookup, WcmpWeights will handle that.
    */

    return entries;
}

Ptr<Ipv4Route>
WcmpStaticRouting :: LookupWcmp(Ipv4Address dest, uint32_t hash_val)
{   
    // Get equal cost LPM paths
    std::vector<Ipv4RoutingTableEntry*> entries = this->MultiLpm(dest);

    if (!entries.size()) {
        // No routes exist
        NS_LOG_LOGIC("LPM returned empty for " << dest);
        return nullptr;
    }

    // Choose a routing table entry
    Ipv4RoutingTableEntry *chosen = this->weights.choose(entries, hash_val);

    if (!chosen) {
        // All interfaces are down
        NS_LOG_LOGIC("All interfaces are down");
        return nullptr;
    }

    // Output the routing entry
    Ptr<Ipv4Route> rtentry = Create<Ipv4Route>();
    rtentry->SetDestination(chosen->GetDest());
    rtentry->SetSource(m_ipv4->SourceAddressSelection(chosen->GetInterface(), chosen->GetDest()));
    rtentry->SetGateway(chosen->GetGateway());
    rtentry->SetOutputDevice(m_ipv4->GetNetDevice(chosen->GetInterface()));

    NS_LOG_LOGIC("WCMP lookup chose " << chosen->GetInterface());

    return rtentry;
}

Ptr<Ipv4Route>
WcmpStaticRouting :: RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << p << header << oif << sockerr);
    Ipv4Address destination = header.GetDestination();
    Ptr<Ipv4Route> rtentry = nullptr;

    if (destination.IsLocalMulticast())
    {
        NS_ABORT_MSG("Multicast not handled yet!");
    }

    if (oif) {
        NS_ABORT_MSG("Will not implement per-interface output yet!");
    }

    // Hash the packet
    uint32_t hash_val = this->hasher.getHash(p, header);
    NS_LOG_LOGIC("WCMP hash for packet = " << hash_val);

    // Lookup for a route
    rtentry = LookupWcmp(destination, hash_val);

    if (rtentry)
    {
        sockerr = Socket::ERROR_NOTERROR;
    }
    else
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
    }
    return rtentry;
}


bool
WcmpStaticRouting :: RouteInput(Ptr<const Packet> p,
                              const Ipv4Header& ipHeader,
                              Ptr<const NetDevice> idev,
                              const UnicastForwardCallback& ucb,
                              const MulticastForwardCallback& mcb,
                              const LocalDeliverCallback& lcb,
                              const ErrorCallback& ecb)
{
    NS_ASSERT(m_ipv4);
    // Check if input device supports IP
    NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
    uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);

    // Multicast recognition; handle local delivery here

    if (ipHeader.GetDestination().IsMulticast())
    {
        NS_ABORT_MSG("Multicast not implemented yet");
    }

    if (m_ipv4->IsDestinationAddress(ipHeader.GetDestination(), iif))
    {
        if (!lcb.IsNull())
        {
            NS_LOG_LOGIC("Local delivery to " << ipHeader.GetDestination());
            lcb(p, ipHeader, iif);
            return true;
        }
        else
        {
            // The local delivery callback is null.  This may be a multicast
            // or broadcast packet, so return false so that another
            // multicast routing protocol can handle it.  It should be possible
            // to extend this to explicitly check whether it is a unicast
            // packet, and invoke the error callback if so
            return false;
        }
    }

    // Check if input device supports IP forwarding
    if (!m_ipv4->IsForwarding(iif))
    {
        NS_LOG_LOGIC("Forwarding disabled for this interface");
        ecb(p, ipHeader, Socket::ERROR_NOROUTETOHOST);
        return true;
    }

    // Hash the packet
    uint32_t hash_val = this->hasher.getHash(p, ipHeader);
    NS_LOG_LOGIC("WCMP hash for packet = " << hash_val);

    // Next, try to find a route
    Ptr<Ipv4Route> rtentry = LookupWcmp(ipHeader.GetDestination(), hash_val);

    if (rtentry)
    {
        NS_LOG_LOGIC("Found unicast destination- calling unicast callback");
        ucb(rtentry, p, ipHeader); // unicast forwarding callback
        return true;
    }
    else
    {
        NS_LOG_LOGIC("Did not find unicast destination- returning false");
        return false; // Let other routing protocols try to handle this
    }
}

bool
WcmpStaticRouting :: LookupRoute(const Ipv4RoutingTableEntry& route, uint32_t metric)
{
    for (auto j = m_networkRoutes.begin(); j != m_networkRoutes.end(); j++)
    {
        Ipv4RoutingTableEntry* rtentry = j->first;

        if (rtentry->GetDest() == route.GetDest() &&
            rtentry->GetDestNetworkMask() == route.GetDestNetworkMask() &&
            rtentry->GetGateway() == route.GetGateway() &&
            rtentry->GetInterface() == route.GetInterface() && j->second == metric)
        {
            return true;
        }
    }
    return false;
}

void 
WcmpStaticRouting :: AddNetworkRouteTo(Ipv4Address network,
    Ipv4Mask networkMask,
    uint32_t interface,
    uint32_t metric)
{
    Ipv4RoutingTableEntry route =
        Ipv4RoutingTableEntry::CreateNetworkRouteTo(network, networkMask, interface);
    if (!LookupRoute(route, metric))
    {
        auto routePtr = new Ipv4RoutingTableEntry(route);

        m_networkRoutes.emplace_back(routePtr, metric);
    }
}

void
WcmpStaticRouting :: AddWildcardRoute(uint32_t interface, uint32_t metric)
{
    AddNetworkRouteTo(Ipv4Address("0.0.0.0"), Ipv4Mask::GetZero(), interface, metric);
}

void 
WcmpStaticRouting :: SetInterfaceWeight(uint32_t interface, uint16_t weight) {
    this->weights.set_weight(interface, weight);
}

void 
WcmpStaticRouting :: NotifyInterfaceUp(uint32_t i) {
    /**
     * We will not add a network route by default (this differs from the behavior
     * of NS-3 and Ipv4StaticRouting).
     * For debug and some other things, we optionally let that functionality be here,
     * but the only thing that we always do, is update the WcmpWeight object.
    */
    weights.add_interface(i);

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
    weights.add_interface(interface);

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

uint32_t 
WcmpStaticRouting :: GetNRoutes() const {
    return this->m_networkRoutes.size();
}

uint32_t 
WcmpStaticRouting :: GetMetric(uint32_t index) const{
    uint32_t tmp = 0;
    for (auto j = m_networkRoutes.begin(); j != m_networkRoutes.end(); j++)
    {
        if (tmp == index)
        {
            return j->second;
        }
        tmp++;
    }
    return 0;
}

Ipv4RoutingTableEntry 
WcmpStaticRouting :: GetRoute(uint32_t index) const{
    uint32_t tmp = 0;
    for (auto j = m_networkRoutes.begin(); j != m_networkRoutes.end(); j++)
    {
        if (tmp == index)
        {
            return j->first;
        }
        tmp++;
    }

    return nullptr;
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
        << ", WcmpStaticRouting table" << std::endl;

    if (GetNRoutes()) {
        *os << "Destination     Metric Iface    Weight State"
            << std::endl;
        for (uint32_t j = 0; j < m_networkRoutes.size(); ++j) {
            std::ostringstream dest;
            Ipv4RoutingTableEntry entry = GetRoute(j);
            dest << entry.GetDest();
            *os << std::setw(16) << dest.str();
            *os << std::setw(7) << GetMetric(j);
            
            if (!Names::FindName(m_ipv4->GetNetDevice(entry.GetInterface())).empty())
            {
                *os << std::setw(9) << Names::FindName(m_ipv4->GetNetDevice(entry.GetInterface()));
            }
            else
            {
                *os << std::setw(9) << entry.GetInterface();
            }

            *os << std::setw(7) << weights.get_weight(entry.GetInterface());

            if (m_ipv4->IsUp(entry.GetInterface())) {
                *os << "Up";
            }
            else {
                *os << "Down";
            }

            *os << std::endl;
        }
    }

    *os << std::endl;
    // Restore the previous ostream state
    (*os).copyfmt(oldState);
}



} // namespace wcmp
} // namespace ns3
