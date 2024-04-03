#ifndef WCMP_WEIGHTS_H
#define WCMP_WEIGHTS_H

#include "ns3/ipv4.h"
#include "ns3/ptr.h"
#include <map>


namespace ns3 {
namespace wcmp {

class WcmpWeights {
    /**
     * The main abstraction of WCMP weights on a node.
    */

    WcmpWeights(Ptr<Ipv4> ipv4);

    private:
        /**
         * This maps interface index to a pair of (weight, up), where
         *  - weight is the actual weight in uint16_t
         *  - up is a boolean, signifying whether or not the interface is up
        */
        std::map<uint32_t, std::pair<uint16_t, bool>> weights;

        /**
         * The pointer to the IPv4 stack object
        */
        Ptr<Ipv4> m_ipv4;

    public:
        uint16_t get_weight(uint32_t if_index) {
            return weights[if_index].first;
        }

        bool is_if_up(uint32_t if_index) {
            return weights[if_index].second;
        }

        void set_weight(uint32_t if_index, uint16_t weight) {
            this->weights[if_index].first = weight;
        }

        void set_state(uint32_t if_index, bool state) {
            this->weights[if_index].second = state;
        }

        Ptr<Ipv4> get_ipv4() {
            return this->m_ipv4;
        }

        /**
         * Given a list of interface indices, choose one according to the given hash
         * and output the interface index for it.
        */
        uint32_t choose(std::vector<uint32_t> output_ifs, uint32_t hash_val);
};

} // Namespace wcmp
} // Namespace ns3

#endif /* WCMP_WEIGHTS_H */