#include "wcmp-static-routing.h"
#include "ns3/enum.h"


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
            );
    
    return tid;
}

WcmpStaticRouting :: WcmpStaticRouting() 
    : m_ipv4(nullptr)
{
    NS_LOG_FUNCTION(this);
}




} // namespace wcmp
} // namespace ns3
