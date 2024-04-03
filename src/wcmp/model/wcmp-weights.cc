#include "wcmp-weights.h"


namespace ns3 {
namespace wcmp {

WcmpWeights :: WcmpWeights (Ptr<Ipv4> ipv4) {
    this->m_ipv4 = ipv4;

    // Ignore Loopback index (0)
    for (uint32_t if_index = 1; if_index < m_ipv4->GetNInterfaces(); if_index++) {
        this->weights[if_index] = std::make_pair(
            (uint16_t) 1, ipv4->IsUp(if_index)
        );
    }
}

uint32_t
WcmpWeights :: choose(std::vector<uint32_t> output_ifs, uint32_t hash_val) {
    // Loop and get the total weight of active interfaces
    uint32_t sum = 0;
    std::vector<std::pair<uint32_t, uint32_t>> up_interfaces;
    for (auto const & it: this->weights) {
        if (it.second.second) {
            sum += it.second.first;
            up_interfaces.push_back(std::make_pair(it.first, sum));
        }
    }

    // This should not happen
    NS_ABORT_IF(!sum);

    // Now, we have a list of (index, upper_bound), we get a random number
    // and check in which chunk it lands.

    std::vector<std::pair<uint32_t, uint32_t>>::iterator it;
    for (it = up_interfaces.begin(); it != up_interfaces.end(); it++)
        if (hash_val < it->second)
            return it->first;
    return (--it)->first;
}
    
} // namespace wcmp
} // namespace ns3
