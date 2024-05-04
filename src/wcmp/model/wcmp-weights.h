#ifndef WCMP_WEIGHTS_H
#define WCMP_WEIGHTS_H

#include "ns3/ipv4.h"
#include "ns3/ptr.h"
#include <map>


namespace ns3 {

class Ipv4RoutingTableEntry;

namespace wcmp {

#define _GET_LEVELED_IF(l, i) (((uint32_t)l << 16) + i)
#define DEFAULT_WCMP_WEIGHT 100

class WcmpWeights {
    /**
     * The main abstraction of WCMP weights on a node.
    */

    private:
        const uint16_t m_levels;
        bool m_debug = false;

        /**
         * We have two maps:
         *  - A map of interface index to a boolean, saying whether the interface is up or down
         *  - A map of interface index (and pod number if needed) to a weight value
         * Usually, we use a uint32_t of the form <pod_num, if_index> as the key of the second map.
        */
        mutable std::unordered_map<uint32_t, bool> states;
        mutable std::map<uint32_t, uint16_t> weights;

        /**
         * The pointer to the IPv4 stack object
        */
        Ptr<Ipv4> m_ipv4;

        /**
         * Reset the weights
        */
        void reset();

    public:
        WcmpWeights();
        WcmpWeights(uint16_t levels);
        WcmpWeights(Ptr<Ipv4> ipv4);
        WcmpWeights(Ptr<Ipv4> ipv4, uint16_t levels);

        uint16_t get_weight(uint32_t if_index, uint16_t level) const {
            return weights[_GET_LEVELED_IF(level, if_index)];
        }

        bool is_if_up(uint32_t if_index) {
            return states[if_index];
        }

        void set_weight(uint32_t if_index, uint16_t level, uint16_t weight) {
            this->weights[_GET_LEVELED_IF(level, if_index)] = weight;
        }

        void set_state(uint32_t if_index, bool state) {
            this->states[if_index] = state;
        }

        Ptr<Ipv4> get_ipv4() {
            return this->m_ipv4;
        }

        const uint16_t get_levels() {
            return this->m_levels;
        }

        /**
         * This function sets the IPv4 stack instance.
         * It is not safe to call this multiple times and it will kill
         * the program if there already is a stack.
        */
        void set_ipv4(Ptr<Ipv4> ipv4);

        /**
         * Given a list of interface indices, choose one according to the given hash
         * and output the interface index for it.
        */
        // uint32_t choose(std::vector<uint32_t> output_ifs, uint32_t hash_val);
        Ipv4RoutingTableEntry* choose(std::vector<Ipv4RoutingTableEntry*> equal_cost_entries, uint32_t hash_val, uint16_t level=0);
        Ipv4RoutingTableEntry* chooseEcmp(std::vector<Ipv4RoutingTableEntry*> equal_cost_entries, uint32_t hash_val);

        /**
         * Add a new interface to the list of tracked interfaces
         * If the interface exists, this does nothing.
        */
        void add_interface(uint32_t if_index, uint16_t weight = (uint16_t) DEFAULT_WCMP_WEIGHT);
};

} // Namespace wcmp
} // Namespace ns3

#endif /* WCMP_WEIGHTS_H */