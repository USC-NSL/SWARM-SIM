#include "wcmp-hasher.h"

#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"


namespace ns3
{
namespace wcmp
{

WcmpHasher :: WcmpHasher() {
}

uint32_t
WcmpHasher :: getHashIpv4(Ptr<const Packet> p, const Ipv4Header& header) {
    uint8_t buf[8];

    header.GetSource().Serialize(buf);
    header.GetDestination().Serialize(buf + 4);

    this->m_hasher.clear();
    return this->m_hasher.GetHash32((char*) buf, 8);
}

uint32_t
WcmpHasher :: getHashIpv4Tcp(Ptr<const Packet> p, const Ipv4Header& header) {
    NS_ASSERT(header.GetProtocol() == TCP_PROTOCOL);
    uint8_t buf[12];
    
    TcpHeader tcpHeader;
    p->PeekHeader(tcpHeader);
    
    header.GetSource().Serialize(buf);
    header.GetDestination().Serialize(buf + 4);
    uint16_t port = tcpHeader.GetSourcePort();
    memcpy(buf + 8, &port, 2);
    port = tcpHeader.GetDestinationPort();
    memcpy(buf + 10, &port, 2);

    this->m_hasher.clear();
    return this->m_hasher.GetHash32((char *) buf, 12);
}

uint32_t
WcmpHasher :: getHashIpv4TcpUdp(Ptr<const Packet> p, const Ipv4Header& header) {
    NS_ASSERT(header.GetProtocol() == UDP_PROTOCOL || header.GetProtocol() == TCP_PROTOCOL);
    uint8_t buf[12];
    
    if (header.GetProtocol() == UDP_PROTOCOL) {
        UdpHeader udpHeader;;
        p->PeekHeader(udpHeader);
        
        header.GetSource().Serialize(buf);
        header.GetDestination().Serialize(buf + 4);
        uint16_t port = udpHeader.GetSourcePort();
        memcpy(buf + 8, &port, 2);
        port = udpHeader.GetDestinationPort();
        memcpy(buf + 10, &port, 2);

        this->m_hasher.clear();
        return this->m_hasher.GetHash32((char *) buf, 13);
    }
    else {
        return this->getHashIpv4Tcp(p, header);
    }
}

uint32_t 
WcmpHasher :: getHash(Ptr<const Packet> p, const Ipv4Header& header) {
    switch (this->hash_algorithm)
    {
        case HASH_IP_ONLY:
            return this->getHashIpv4(p, header);
        case HASH_IP_TCP:
            return this->getHashIpv4Tcp(p, header);
        case HASH_IP_TCP_UDP:
            return this->getHashIpv4TcpUdp(p, header);
        default:
            NS_ABORT_MSG("Bad hash alg");
    }
}

std::string 
WcmpHasher :: dump_packet(Ptr<const Packet> p, const Ipv4Header& header) {
    std::string out;
    
    if (header.GetProtocol() == UDP_PROTOCOL) {
        out += "UDP: ";
        UdpHeader udpHeader;;
        p->PeekHeader(udpHeader);
        
        out += "Source = ";
        out += std::to_string(udpHeader.GetSourcePort());
        out += " Destination = ";
        out += std::to_string(udpHeader.GetDestinationPort());
    }
    else {
        out += "TCP: ";
        TcpHeader tcpHeader;;
        p->PeekHeader(tcpHeader);
        
        out += "Source = ";
        out += std::to_string(tcpHeader.GetSourcePort());
        out += " Destination = ";
        out += std::to_string(tcpHeader.GetDestinationPort());
    }

    return out;
}
    
} // namespace wcmp
} // namespace ns3
