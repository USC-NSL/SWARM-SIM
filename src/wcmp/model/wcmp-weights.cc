#include "wcmp-weights.h"
#include "ns3/ipv4-routing-table-entry.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WcmpWeights");

namespace wcmp {

WcmpWeights :: WcmpWeights () {
    this->m_ipv4 = nullptr;
}

WcmpWeights :: WcmpWeights (Ptr<Ipv4> ipv4) {
    NS_ABORT_IF(m_ipv4);
    this->m_ipv4 = ipv4;
    reset();
}

void
WcmpWeights :: reset () {
    // Ignore Loopback index (0)
    for (uint32_t if_index = 1; if_index < m_ipv4->GetNInterfaces(); if_index++) {
        this->weights[if_index] = std::make_pair(
            (uint16_t) 1, m_ipv4->IsUp(if_index)
        );
    }
}

void 
WcmpWeights :: set_ipv4(Ptr<Ipv4> ipv4) {
    NS_ABORT_IF(m_ipv4);
    this->m_ipv4 = ipv4;
    reset();
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

    if (up_interfaces.size() == 0) {
        // All interfaces are down. We output interface 0.
        // Interface 0 is loopback, so it shouldn't be used anyway and the caller
        // can see from this that the packet needs to be dropped.
        return 0;
    }
    else if (up_interfaces.size() == 1) {
        // No need to choose!
        return up_interfaces.at(0).first;
    }

    /**
     * We'll be doing this per-packet, so even small time savers are helpful.
     * We avoid doing floating-point division here for that reason, to this end,
     * doing this normall would entail dividing the hash value `H` by the maximum 
     * uint32_t value, `M` and getting r := H/M * (sum), and seeing where that
     * lands on the bounds.
     * To do this without floats, we will just compare r * M = (H * sum) to each
     * bound times M.
    */

    std::vector<std::pair<uint32_t, uint32_t>>::iterator it;
    for (it = up_interfaces.begin(); it != up_interfaces.end(); it++)
        if ((uint64_t) hash_val * sum < (uint64_t) it->second * UINT32_MAX)
            return it->first;
    return (--it)->first;
}

Ipv4RoutingTableEntry* 
WcmpWeights :: choose(std::vector<Ipv4RoutingTableEntry*> equal_cost_entries, uint32_t hash_val) {
    // Loop and get the total weight of active interfaces
    uint32_t sum = 0;
    uint32_t if_index;

    NS_LOG_LOGIC("Choosing from  " << equal_cost_entries.size() << " equal cost entries");

    std::vector<std::pair<Ipv4RoutingTableEntry*, uint32_t>> up_entries;
    for (auto const & it: equal_cost_entries) {
        if_index = it->GetInterface();
        NS_LOG_LOGIC("Considerng " << if_index);
        // Is the interface up?
        if (this->weights.at(if_index).second) {
            NS_LOG_LOGIC("Considerng up interface " << if_index);
            sum += this->weights.at(if_index).first;
            up_entries.push_back(std::make_pair(it, sum));
            NS_LOG_LOGIC("Current sum " << sum);
        }
        else {
            NS_LOG_LOGIC("Ignoring interface " << if_index);
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
        NS_LOG_LOGIC("Comparing " << (uint64_t) hash_val * sum << " and " << (uint64_t) it->second * UINT32_MAX);
        if ((uint64_t) hash_val * sum < (uint64_t) it->second * UINT32_MAX) {
            return it->first;
        }
    }
    return (--it)->first;
}

void
WcmpWeights :: add_interface(uint32_t if_index, uint16_t weight) {
    if (this->weights.count(if_index))
        return;
    this->weights[if_index] = std::make_pair(
        (uint16_t) 1, this->m_ipv4->IsUp(if_index)
    );
}
    
} // namespace wcmp
} // namespace ns3
