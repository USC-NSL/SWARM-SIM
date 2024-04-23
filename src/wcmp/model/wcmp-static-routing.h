#ifndef WCMP_STATIC_ROUTING_H
#define WCMP_STATIC_ROUTING_H

#include <functional>
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

/**
 * Template for function that maps an Ipv4 destination address to a 
 * level index.
*/
typedef std::function<uint16_t(Ipv4Address)> level_mapper_func;

/**
 * Template for function that is invoked when an interface goes down/up.
 * By default, it only uses the interface index and the local data in the
 * routing protocol instance.
*/
typedef std::function<void(uint32_t)> if_up_down_func;

class Node;

namespace wcmp
{

class WcmpStaticRouting : public Ipv4RoutingProtocol {
    private:
        /// The IPv4 stack instance
        Ptr<Ipv4> m_ipv4;

        /// Hash algorithm to use
        hash_alg_t m_hash_alg;

        /// Whether or not to add a route when an interface comes up
        bool m_add_route_on_up;

        /// Whether or not to use the WCMP cache
        bool m_use_cache;

        /// Number of levels for the WCMP hash atable
        uint16_t m_levels;

        /// Pointer to a mapping function from destiation address to level index
        level_mapper_func m_level_mapper_func = nullptr;

        /// Pointer to a function that handles interface up/down
        if_up_down_func m_if_up_func = nullptr;
        if_up_down_func m_if_down_func = nullptr;

        /// The WCMP hash calculator
        WcmpHasher hasher;

        /// WCMP weights
        WcmpWeights weights;

        /// WCMP hash cache, speeds up lookup
        std::unordered_map<uint32_t, std::vector<std::pair<Ipv4Address, Ipv4RoutingTableEntry*>>> wcmp_cache;

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

        void InvalidateCache() {
            wcmp_cache.clear();
        }

        Ipv4RoutingTableEntry* LookupCache(uint32_t hash_val, Ipv4Address dest) {
            auto res = wcmp_cache.find(hash_val);
            if (res == wcmp_cache.end())
                return nullptr;
            
            for (auto elem = res->second.begin(); elem != res->second.end(); elem++) {
                if (elem->first == dest)
                    return elem->second;
            }

            NS_ABORT_MSG("Invalid cache miss, this should not happen!");
        }

        void UpdateCache(uint32_t hash_val, Ipv4Address dest, Ipv4RoutingTableEntry *entry) {
            auto res = wcmp_cache.find(hash_val);
            if (res == wcmp_cache.end())
                wcmp_cache[hash_val] = std::vector<std::pair<Ipv4Address, Ipv4RoutingTableEntry*>>{std::make_pair(dest, entry)};
            else
                wcmp_cache[hash_val].push_back(std::make_pair(dest, entry));
        }

    public:
        static TypeId GetTypeId();

        WcmpStaticRouting();
        WcmpStaticRouting(uint16_t level);
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
        void SetInterfaceWeight(uint32_t interface, uint16_t level, uint16_t weight);
        
        void SetMapperFunction(level_mapper_func f) {
            this->m_level_mapper_func = f;
        }

        void SetIfDownFunction(if_up_down_func f) {
            this->m_if_down_func = f;
        }
        void SetIfUpFunction(if_up_down_func f) {
            this->m_if_up_func = f;
        }

        uint32_t GetNRoutes() const;
        uint32_t GetMetric(uint32_t index) const;
        uint16_t GetLevels() const;
        Ipv4RoutingTableEntry GetRoute(uint32_t index) const;
};

} // namespace wcmp
} // namespace ns3


#endif /* WCMP_STATIC_ROUTING_H */