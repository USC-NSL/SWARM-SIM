#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/netanim-module.h"
#include "ns3/internet-module.h"

using namespace std;


/**
 * Component name
*/
const char* COMPONENT_NAME = "SWARMSimulation";

/**
 * Animation file output
*/
string ANIM_FILE_OUTPUT = "swarm-anim.xml";


/**
 * P2P Link Attributes
*/
const uint32_t DEFAULT_LINK_RATE = 10;          // Gbps
const uint32_t DEFAULT_LINK_DELAY = 5000;         // us


/**
 * IPv4 Address Assignement Params
*/
const char lanIpv4AddressBase[] = "10.0.0.0";
const char lanIpv4AddressMask[] = "255.255.255.252";
const char serverIpv4AddressBase[] = "192.168.0.0";
const char serverIpv4AddressMask[] = "255.255.255.252";
const char naiveIpv4AddressBase[] = "10.0.0.0";
const char naiveIpv4AddressMask[] = "255.255.255.252";


/**
 * Default switch radix and number of pods
*/
const uint32_t DEFAULT_SWITCH_RADIX = 4;
const uint32_t DEFAULT_NUM_PODS = 2;


/**
 * Default number of servers per edge switch
*/
const uint32_t DEFAULT_NUM_SERVERS = DEFAULT_SWITCH_RADIX / 2;

/**
 * Links are identified as `(LEVEL_1, i, LEVEL_2, j)` where:
 *  - LEVEL_1 and LEVEL_2 denotes the level of the source and destination interfaces
 *    of this link.
 *  - `i` and `j` are indicees for the switch number in the associated level, starting
 *    from left.
*/
typedef enum topology_level_t {
    EDGE, AGGREGATE, CORE
} topology_level;


/**
 * Some constants for animation file outputs
*/
#define CORE_Y 0.0
#define AGG_Y 100.0
#define EDGE_Y 200.0
#define SERVER_Y 250.0
#define SERVER_DELTA 30.0
#define WIDTH 600.0
#define NODE_SIZE 8.0


/**
 * The struct that will keep the topology parameters
*/
typedef struct topology_descriptor_t {
    uint32_t linkRate = DEFAULT_LINK_RATE;
    uint32_t linkDelay = DEFAULT_LINK_DELAY;
    uint32_t switchRadix = DEFAULT_SWITCH_RADIX;
    uint32_t numServers = DEFAULT_NUM_SERVERS;
    uint32_t numPods = DEFAULT_NUM_PODS;
    bool animate = false;
} topolgoy_descriptor;

/**
 * This class will keep the topology node containers
 * 
 * Switches in each layer are indexed from 0, starting from
 * the left all the way to the right. This index is used to track
 * links to that switch.
 * 
 * Switches are also addressed with the same index relative to the
 * pod that they are part of, which is how they are addressed in the
 * node container.
 * 
 * So for example, the following topology:
 * 
 *      +--------+            +--------+
 *      | CORE 1 |    ...     | CORE 4 |
 *      +--------+            +--------+
 * 
 *  +-------+ +-------+    +-------+ +-------+
 *  | AGG 1 | | AGG 2 |    | AGG 3 | | AGG 4 |
 *  +-------+ +-------+    +-------+ +-------+
 *  \_________________/    \_________________/
 *        POD 1                   POD 2
 * 
 * Switch `AGG 3` can be referred to as either:
 *  - The single uint32_t: 2
 *  - The pair of uint32_t: {1, 0}
 * 
 * We refer to the first one as the `index` and the second one as the 
 * `tuple` of the switch.
 * 
 * Core switches have no tuple addressing, they are just addressed with
 * their index.
*/
class ClosTopology {
    private:
        // The `Even` container connects to aggregate switches with even index,
        // The `Odd` container to odd indices.
        ns3::NodeContainer coreSwitchesEven;
        ns3::NodeContainer coreSwitchesOdd;

        // Each container, holds the switches for a specific pod
        vector<ns3::NodeContainer> aggSwitches;
        vector<ns3::NodeContainer> edgeSwitches;

        // Map edge switch index to the container of servers under that ToR
        map<uint32_t, ns3::NodeContainer> servers;

        // Map the index of source and destination switches to the associated NetDevice pair
        map<tuple<uint32_t, uint32_t>, ns3::NetDeviceContainer> edgeToAggLinks;
        map<tuple<uint32_t, uint32_t>, ns3::NetDeviceContainer> aggToCoreLinks;
        map<tuple<uint32_t, uint32_t>, ns3::NetDeviceContainer> serverToEdgeLinks;

        // These hold the NIC and IPv4 interfaces of the servers, given the ToR index
        map<uint32_t, ns3::NetDeviceContainer> serverDevices;
        map<uint32_t, ns3::Ipv4InterfaceContainer> serverInterfaces;

        // Application containers for each server
        vector<ns3::ApplicationContainer> serverApplications;
        
        ns3::AnimationInterface *anim = NULL;

        void createServers();
        void connectServers();

        // For NetAnim
        void setNodeCoordinates();

    public:
        topology_descriptor_t params;
        ClosTopology(const topology_descriptor_t m_params);
        void createTopology();
        void createLinks();
        void assignIpsLan();
        void assignIpsNaive();
        void echoBetweenHosts(uint32_t client_host, uint32_t server_host, double interval=0.1);
        void unidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate="2Mbps");

        ns3::Ipv4InterfaceContainer getTorServerInterfaces(uint32_t edge_idx) {
            return this->serverInterfaces[edge_idx];
        }

        ns3::Ipv4Address getServerAddress(uint32_t host_idx) {
            uint32_t server_idx = host_idx % this->params.numServers;
            uint32_t edge_idx = host_idx / this->params.numServers;

            return this->getServerAddress(edge_idx, server_idx);
        }

        ns3::Ipv4Address getServerAddress(uint32_t edge_idx, uint32_t server_idx) {
            return this->serverInterfaces[edge_idx].GetAddress(server_idx);
        }

        ns3::NetDeviceContainer getLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx) {
            if (src_level < dst_level) {
                if (src_level == EDGE) {
                    NS_ASSERT(dst_level == AGGREGATE);
                    return this->edgeToAggLinks[make_tuple(src_idx, dst_idx)];
                }
                else {
                    NS_ASSERT(src_level == AGGREGATE && dst_level == CORE);
                    return this->aggToCoreLinks[make_tuple(src_idx, dst_idx)];
                }
            }
            else {
                if (dst_level == EDGE) {
                    NS_ASSERT(src_level == AGGREGATE);
                    return this->edgeToAggLinks[make_tuple(dst_idx, src_idx)];
                }
                else {
                    NS_ASSERT(dst_level == AGGREGATE && src_level == CORE);
                    return this->aggToCoreLinks[make_tuple(dst_idx, src_idx)];
                }
            }
        }

        ns3::Ptr<ns3::Node> getCore(uint32_t idx) {
            if (idx < this->params.switchRadix / 2) {
                return this->coreSwitchesEven.Get(idx);
            }
            return this->coreSwitchesOdd.Get(idx - this->params.switchRadix / 2);
        }

        ns3::Ptr<ns3::Node> getAggregate(uint32_t pod_num, uint32_t idx) {
            return this->aggSwitches[pod_num].Get(idx);
        }

        ns3::Ptr<ns3::Node> getAggregate(uint32_t  full_idx) {
            uint32_t idx = full_idx % this->params.switchRadix / 2;
            uint32_t pod_num = full_idx / this->params.switchRadix / 2;
            return this->getAggregate(pod_num, idx);
        }

        ns3::Ptr<ns3::Node> getEdge(uint32_t pod_num, uint32_t idx) {
            return this->edgeSwitches[pod_num].Get(idx);
        }

        ns3::Ptr<ns3::Node> getEdge(uint32_t  full_idx) {
            uint32_t idx = full_idx % this->params.switchRadix / 2;
            uint32_t pod_num = full_idx / this->params.switchRadix / 2;
            return this->getEdge(pod_num, idx);
        }

        ns3::Ptr<ns3::Node> getHost(uint32_t edge_idx, uint32_t host_idx) {
            return this->servers[edge_idx].Get(host_idx);
        }

        ns3::Ptr<ns3::Node> getHost(uint32_t host_idx) {
            uint32_t idx = host_idx % this->params.numServers;
            uint32_t edge_idx = host_idx / this->params.numServers;
            return this->getHost(edge_idx, idx);
        }

        void startApplications(double t_start, double t_finish) {
            for (auto const &container: this->serverApplications) {
                container.Start(ns3::Seconds(t_start));
                container.Stop(ns3::Seconds(t_finish));
            }
        }
};

/**
 * Misc. definitions
*/
#define UDP_DISCARD_PORT 9
#define UDP_PACKET_SIZE_BIG 1024
#define UDP_PACKET_SIZE_SMALL 64

/**
 * Function pointer typedefs
*/
typedef void (*link_attribute_change_func)(ClosTopology*, topology_level, uint32_t, topology_level, uint32_t, const string);
typedef void (*link_state_change_func)(ClosTopology*, topology_level, uint32_t, topology_level, uint32_t);

/**
 * Function declarations
*/
void logDescriptors(topolgoy_descriptor *topo_params);
void changeBandwidth(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr);
void changeDelay(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr);
void disableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);
void enableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);
template<typename... Args> void schedule(double t, link_state_change_func func, Args... args);
template<typename... Args> void schedule(double t, link_attribute_change_func func, Args... args);
