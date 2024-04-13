#ifndef WCMP_STATIC_ROUTING_H
#define WCMP_STATIC_ROUTING_H

#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/ipv4-routing-protocol.h"
#include "wcmp-hasher.h"
#include "wcmp-weights.h"

/**
 * We implement WCMP as an extension to static routing.
 * Essentially, the system collects equal weight static routes, and then it
 * will load balance on them given the associated weights.
 * 
 * This stack needs to be manually loaded onto each participating node,
 * if not, default static/global routing will take priority over it.
*/

namespace ns3
{

class Node;

namespace wcmp
{

/**
 * Typedef forpPointer to a function that maps destiation Ipv4Address
 * to a level index.
*/
typedef uint16_t (*level_mapper_func)(Ipv4Address);

uint16_t constant_map(Ipv4Address dest) {
    return (uint16_t) 0;
}

class WcmpStaticRouting : public Ipv4RoutingProtocol {
    private:
        /// The IPv4 stack instance
        Ptr<Ipv4> m_ipv4;

        /// Hash algorithm to use
        hash_alg_t m_hash_alg;

        /// Whether or not to add a route when an interface comes up
        bool m_add_route_on_up;

        /// Pointer to a mapping function from destiation address to level indexx
        level_mapper_func m_level_mapper_func = &constant_map;

        /// The WCMP hash calculator
        WcmpHasher hasher;

        /// WCMP weights
        WcmpWeights weights;

        /// Container for the network routes
        typedef std::list<std::pair<Ipv4RoutingTableEntry*, uint32_t>> NetworkRoutes;

        /// Const Iterator for container for the network routes
        typedef std::list<std::pair<Ipv4RoutingTableEntry*, uint32_t>>::const_iterator NetworkRoutesCI;

        /// Iterator for container for the network routes
        typedef std::list<std::pair<Ipv4RoutingTableEntry*, uint32_t>>::iterator NetworkRoutesI;

        /// The forwarding table for network.
        NetworkRoutes m_networkRoutes;

        std::vector<Ipv4RoutingTableEntry*> MultiLpm(Ipv4Address dest);

        bool LookupRoute(const Ipv4RoutingTableEntry& route, uint32_t metric);

    protected:
        void DoDispose() override;

    public:
        static TypeId GetTypeId();

        WcmpStaticRouting();
        WcmpStaticRouting(uint16_t level, level_mapper_func f);
        ~WcmpStaticRouting() override;

        /**
         * The main lookup function that implements WCMP
        */
        Ptr<Ipv4Route> LookupWcmp(Ipv4Address dest, uint32_t hash_val);
        Ptr<Ipv4Route> LookupWcmp(Ipv4Address dest, uint32_t hash_val, uint32_t iif);

        Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
            const Ipv4Header& header,
            Ptr<NetDevice> oif,
            Socket::SocketErrno& sockerr) override;    

        bool RouteInput(Ptr<const Packet> p,
            const Ipv4Header& header,
            Ptr<const NetDevice> idev,
            const UnicastForwardCallback& ucb,
            const MulticastForwardCallback& mcb,
            const LocalDeliverCallback& lcb,
            const ErrorCallback& ecb) override;

        void NotifyInterfaceUp(uint32_t interface) override;
        void NotifyInterfaceDown(uint32_t interface) override;
        void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
        void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
        void SetIpv4(Ptr<Ipv4> ipv4) override;
        void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
            Time::Unit unit = Time::S) const override;

        void AddNetworkRouteTo(Ipv4Address network,
                            Ipv4Mask networkMask,
                            uint32_t interface,
                            uint32_t metric = 0);

        void AddWildcardRoute(uint32_t interface, uint32_t metric);
        void SetInterfaceWeight(uint32_t interface, uint16_t weight);

        uint32_t GetNRoutes() const;
        uint32_t GetMetric(uint32_t index) const;
        Ipv4RoutingTableEntry GetRoute(uint32_t index) const;
};

} // namespace wcmp
} // namespace ns3


#endif /* WCMP_STATIC_ROUTING_H */