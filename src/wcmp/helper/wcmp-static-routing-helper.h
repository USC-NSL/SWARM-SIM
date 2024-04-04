#ifndef WCMP_STATIC_ROUTING_HELPER_H
#define WCMP_STATIC_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/wcmp-static-routing.h"

namespace ns3
{

class WcmpStaticRoutingHelper : public Ipv4RoutingHelper {
    public:
        WcmpStaticRoutingHelper();
        WcmpStaticRoutingHelper(const WcmpStaticRoutingHelper& o);

        WcmpStaticRoutingHelper& operator=(const WcmpStaticRoutingHelper&) = delete;

        WcmpStaticRoutingHelper* Copy() const override;
        Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

        Ptr<wcmp::WcmpStaticRouting> GetWcmpStaticRouting(Ptr<Ipv4> ipv4) const;

    private:
        ObjectFactory m_factory;
};

} // namespace ns3


#endif /* WCMP_STATIC_ROUTING_HELPER_H */