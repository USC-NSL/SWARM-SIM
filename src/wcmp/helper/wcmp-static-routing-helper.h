#ifndef WCMP_STATIC_ROUTING_HELPER_H
#define WCMP_STATIC_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/wcmp-static-routing.h"

namespace ns3
{

class WcmpStaticRoutingHelper : public Ipv4RoutingHelper {
    public:
        WcmpStaticRoutingHelper();
        WcmpStaticRoutingHelper(uint16_t level, level_mapper_func f);
        WcmpStaticRoutingHelper(const WcmpStaticRoutingHelper& o);

        WcmpStaticRoutingHelper& operator=(const WcmpStaticRoutingHelper&) = delete;

        WcmpStaticRoutingHelper* Copy() const override;
        Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

        Ptr<wcmp::WcmpStaticRouting> GetWcmpStaticRouting(Ptr<Ipv4> ipv4) const;
        void SetInterfaceWeight(Ptr<Ipv4> ipv4, uint32_t interface, uint16_t level, uint16_t weight);

        void doEcmp();
        static void setCaching(bool do_caching) {
            wcmp::WcmpStaticRouting::SetCaching(do_caching);
        }

    private:
        level_mapper_func m_func = nullptr;
        uint16_t m_routing_levels = 1;
};

} // namespace ns3


#endif /* WCMP_STATIC_ROUTING_HELPER_H */