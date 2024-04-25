#include "wcmp-weights.h"
#include "ns3/ipv4-routing-table-entry.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WcmpWeights");

namespace wcmp {

WcmpWeights :: WcmpWeights () 
    :m_levels(1)
{
    this->m_ipv4 = nullptr;
}

WcmpWeights :: WcmpWeights (Ptr<Ipv4> ipv4)
    :m_levels(1)
{
    NS_ABORT_IF(m_ipv4);
    this->m_ipv4 = ipv4;
    reset();
}

WcmpWeights :: WcmpWeights (uint16_t levels) 
    :m_levels(levels)
{
    this->m_ipv4 = nullptr;
}

WcmpWeights :: WcmpWeights (Ptr<Ipv4> ipv4, uint16_t levels)
    :m_levels(levels)
{
    NS_ABORT_IF(m_ipv4);
    this->m_ipv4 = ipv4;
    reset();
}

void
WcmpWeights :: reset () {
    // Ignore Loopback index (0)
    for (uint32_t if_index = 1; if_index < m_ipv4->GetNInterfaces(); if_index++) {
        this->states[if_index] = m_ipv4->IsUp(if_index);
        for (uint16_t level = 0; level < m_levels; level++)
            this->weights[_GET_LEVELED_IF(level, if_index)] = (uint16_t) 1;
    }
}

void 
WcmpWeights :: set_ipv4(Ptr<Ipv4> ipv4) {
    NS_ABORT_IF(m_ipv4);
    this->m_ipv4 = ipv4;
    reset();
}

Ipv4RoutingTableEntry* 
WcmpWeights :: choose(std::vector<Ipv4RoutingTableEntry*> equal_cost_entries, uint32_t hash_val, uint16_t level) {
    printWeights();
    // Loop and get the total weight of active interfaces
    uint32_t sum = 0;
    uint32_t if_index;

    std::vector<std::pair<Ipv4RoutingTableEntry*, uint32_t>> up_entries;
    for (auto const & it: equal_cost_entries) {
        if_index = it->GetInterface();

        // Is the interface up?
        if (this->states[if_index]) {
            sum += this->weights[_GET_LEVELED_IF(level, if_index)];
            up_entries.push_back(std::make_pair(it, sum));
        }
    }

    // This should not happen
    NS_ABORT_IF(!sum);

    if (up_entries.size() == 0) {
        NS_LOG_LOGIC("No Up entries");
        return nullptr;
    }
    else if (up_entries.size() == 1) {
        // No need to choose!
        NS_LOG_LOGIC("Just a single Up entry");
        return up_entries.at(0).first;
    }

    std::vector<std::pair<Ipv4RoutingTableEntry*, uint32_t>>::iterator it;
    for (it = up_entries.begin(); it != up_entries.end(); it++) {
        if ((uint64_t) hash_val * sum < (uint64_t) it->second * UINT32_MAX) {
            return it->first;
        }
    }
    return (--it)->first;
}

void
WcmpWeights :: add_interface(uint32_t if_index, uint16_t weight) {
    if (this->states.count(if_index))
        return;

    this->states[if_index] = this->m_ipv4->IsUp(if_index);

    for (uint16_t level = 0; level < m_levels; level++)
        this->weights[_GET_LEVELED_IF(level, if_index)] = (uint16_t) 1;
}

void
WcmpWeights :: printWeights() {
    printf("Weights\n");
    for (auto const & it: this->weights) {
        printf("%x : %u\n", it.first, it.second);
    }
    printf("\n\n");
}
    
} // namespace wcmp
} // namespace ns3
