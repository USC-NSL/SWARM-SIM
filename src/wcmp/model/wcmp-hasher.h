#ifndef WCMP_HASHER_H
#define WCMP_HASHER_H

#include "ns3/hash.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"


namespace ns3 {
namespace wcmp {

// Ipv4 protocol field values
#define TCP_PROTOCOL (uint8_t)0x06
#define UDP_PROTOCOL (uint8_t)0x11


// We implement 3 hashing algs
typedef enum hash_alg_t {
    HASH_IP_ONLY,        // Hash only the IP header
    HASH_IP_TCP,         // Hash IP and TCP header, ignore UDP
    HASH_IP_TCP_UDP      // Hash IP and TCP/UDP header
} hash_alg;


class WcmpHasher {
    private:
        Hasher m_hasher;
        hash_alg_t hash_algorithm = HASH_IP_TCP_UDP;
        uint32_t salt;

    public:
        /**
         * This implements simple packet hashing for ECMP/WCMP
        */
        WcmpHasher();
        
        hash_alg_t get_hash_alg() {
            return this->hash_algorithm;
        }

        void set_hash_alg(hash_alg_t alg) {
            this->hash_algorithm = alg;
        }

        uint32_t getHashIpv4(Ptr<const Packet> p, const Ipv4Header& header);
        uint32_t getHashIpv4Tcp(Ptr<const Packet> p, const Ipv4Header& header);
        uint32_t getHashIpv4TcpUdp(Ptr<const Packet> p, const Ipv4Header& header);
        uint32_t getHash(Ptr<const Packet> p, const Ipv4Header& header);
};

} // namespace wcmp
} // namespace ns3


#endif /* WCMP_HASHER_H */