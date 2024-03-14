#ifndef WCMP_GLOBAL_ROUTING_H
#define WCMP_GLOBAL_ROUTING_H

#include "ns3/internet-module.h"


using namespace ns3;


typedef enum hash_alg_t {
    HASH_IP_ONLY,        // Hash only the IP header
    HASH_IP_TCP,         // Hash IP and TCP header, ignore UDP
    HASH_IP_TCP_UDP      // Hash IP and TCP/UDP header
} hash_alg;


#define TCP_PROTOCOL (uint8_t)0x06
#define UDP_PROTOCOL (uint8_t)0x11


class Ipv4WcmpGlobalRouting : Ipv4GlobalRouting {
    Ipv4WcmpGlobalRouting();

    private:
        Hasher m_hasher;
        hash_alg_t hash_algorithm = HASH_IP_TCP_UDP;
    
        uint64_t getHashIpv4(Ptr<const Packet> p, const Ipv4Header& header) {
            uint8_t buf[8];

            header.GetSource().Serialize(buf);
            header.GetDestination().Serialize(buf + 4);

            this->m_hasher.clear();
            return this->m_hasher.GetHash32((char*) buf, 8);
        }

        uint64_t getHashIpv4Tcp(Ptr<const Packet> p, const Ipv4Header& header) {
            NS_ASSERT(header.GetProtocol() == TCP_PROTOCOL);
            uint8_t buf[12];
            
            TcpHeader tcpHeader;
            p->PeekHeader(tcpHeader);
            
            header.GetSource().Serialize(buf);
            header.GetDestination().Serialize(buf + 4);
            uint16_t port = tcpHeader.GetSourcePort();
            memcpy(buf + 8, &port, 2);
            uint16_t port = tcpHeader.GetDestinationPort();
            memcpy(buf + 10, &port, 2);

            this->m_hasher.clear();
            return this->m_hasher.GetHash32((char *) buf, 12);
        }

        uint64_t getHashIpv4TcpUdp(Ptr<const Packet> p, const Ipv4Header& header) {
            NS_ASSERT(header.GetProtocol() == UDP_PROTOCOL || header.GetProtocol() == TCP_PROTOCOL);
            uint8_t buf[12];
            
            if (header.GetProtocol() == UDP_PROTOCOL) {
                UdpHeader udpHeader;;
                p->PeekHeader(udpHeader);
                
                header.GetSource().Serialize(buf);
                header.GetDestination().Serialize(buf + 4);
                uint16_t port = udpHeader.GetSourcePort();
                memcpy(buf + 8, &port, 2);
                uint16_t port = udpHeader.GetDestinationPort();
                memcpy(buf + 10, &port, 2);

                this->m_hasher.clear();
                return this->m_hasher.GetHash32((char *) buf, 13);
            }
            else {
                return this->getHashIpv4Tcp(p, header);
            }
        }

        uint64_t getHash(Ptr<const Packet> p, const Ipv4Header& header) {
            switch (this->hash_algorithm)
            {
                case HASH_IP_ONLY:
                    return this->getHashIpv4(p, header);
                case HASH_IP_TCP:
                    return this->getHashIpv4Tcp(p, header);
                case HASH_IP_TCP_UDP:
                    return this->getHashIpv4TcpUdp(p, header);
                default:
                    NS_ASSERT(false);
            }
        }
};

#endif