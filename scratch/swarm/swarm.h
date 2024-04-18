/**
 * Usually, we would just pass compile flags from command line like a normal person,
 * but passing CXXFLAGS or CXXFLAGS_EXTRA from `ns3` is very wonky. 
 * So just set the variables manually from here.
*/

#ifndef SWARM_H
#define SWARM_H

// Use MPI
#ifndef MPI_ENABLED
#define MPI_ENABLED 1
#endif
// Use Netanim
#ifndef NETANIM_ENABLED
#define NETANIM_ENABLED 0
#endif

#include "flow_scheduler.h"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wcmp-static-routing-helper.h"

using namespace std;

#if NETANIM_ENABLED
/**
 * Netanim headers
*/
#include "ns3/netanim-module.h"
#include "ns3/mobility-helper.h"

/**
 * Animation file output
*/
string ANIM_FILE_OUTPUT = "swarm-anim.xml";

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
#endif /* NETANIM_ENABLED */

#if MPI_ENABLED
#include "ns3/mpi-module.h"
#endif /* MPI_ENABLED */

/**
 * When using MPI:
 *  - systemId is the rank of the current process
 *  - systemCount is the number of LPs
*/
uint32_t systemId = 0;
uint32_t systemCount = 1;

/**
 * Component name for logging
*/
const char* COMPONENT_NAME = "SWARMSimulation";


/**
 * P2P Link Attributes
*/
const uint32_t DEFAULT_LINK_RATE = 40;          // Gbps
const uint32_t DEFAULT_LINK_DELAY = 50;         // us


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

typedef enum swarm_log_level_t {
    DEBG, INFO,  WARN
} swarm_log_level;

swarm_log_level current_log_level = INFO;

/**
 * WCMP routing priority.
 * Static routing is 0, so this should be less than that.
*/
#define WCMP_ROUTING_PRIORITY -20

/**
 * Direct and Backup path metrics
*/
#define DIRECT_PATH_METRIC 1
#define BACKUP_PATH_METRIC 10

/**
 * Misc. definitions
*/
#define UDP_DISCARD_PORT 9
#define TCP_DISCARD_PORT 10

#define UDP_PACKET_SIZE_BIG 1024
#define UDP_PACKET_SIZE_SMALL 64

#define TICK_PROGRESS_EVERY_WHAT_PERCENT 1
#define PROGRESS_BAR_WIDTH 70

#define QUIET_INTERVAL_LENGTH 1.0

/**
 * Logging definitions
 * These are basically NS-3 macros, with the exception
 * that they do not get disabled in the optimized build.
*/
#ifndef SWARM_LOG_CONDITION
#define SWARM_LOG_CONDITION if (systemId == 0)
#endif

#define SWARM_SET_LOG_LEVEL(level) current_log_level = level
#define SWARM_LOG_UNCON(msg) std::clog << msg << "\n"

#define SWARM_DEBG_ALL(msg)                                 \
    do {                                                    \
        if (current_log_level <= DEBG)                      \
            SWARM_LOG_UNCON("[DEBG][" << systemId << "] "   \
                << msg);                                    \
    } while (false)                                         \

#define SWARM_INFO_ALL(msg)                                 \
    do {                                                    \
        if (current_log_level <= INFO)                      \
            SWARM_LOG_UNCON("[INFO][" << systemId << "] "   \
                << msg);                                    \
    } while (false)                                         \

#define SWARM_DEBG(msg)                                     \
    SWARM_LOG_CONDITION                                     \
    do {                                                    \
        if (current_log_level <= DEBG)                      \
            SWARM_LOG_UNCON("[DEBG] " << msg);              \
    } while (false)                                         \

#define SWARM_INFO(msg)                                     \
    SWARM_LOG_CONDITION                                     \
    do {                                                    \
        if (current_log_level <= INFO)                      \
            SWARM_LOG_UNCON("[INFO] " << msg);              \
    } while (false)                                         \

#define SWARM_WARN(msg)                                     \
    SWARM_LOG_CONDITION                                     \
    do {                                                    \
        if (current_log_level <= WARN)                      \
            SWARM_LOG_UNCON("[WARN] " << msg);              \
    } while (false)                                         \


/**
 * Drop tail queue max length
 *  @note Not used yet ...
*/
const uint32_t MAX_PACKET_PER_QUEUE = 10;

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
    bool mpi = false;
    bool enableEdgeBounceBackup = false;
} topolgoy_descriptor;

/**
 * Our level mapper function for WCMP stacks on aggregation and edge
 * nodes.
 * We'll declare it like this here and then bind it later.
*/
ns3::level_mapper_func wcmp_level_mapper;

/**
 * We'll use these functions to react to link events
*/
ns3::if_up_down_func wcmp_if_down_func;
ns3::if_up_down_func wcmp_if_up_func;

/**
 * Host flwo dispatcher
*/
host_flow_dispatcher host_flow_dispatcher_function;

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

        // Used to get a free port for a host
        unordered_map<uint32_t, uint16_t> portMap;
 
        #if NETANIM_ENABLED
        ns3::AnimationInterface *anim = NULL;
        void setNodeCoordinates();
        #endif /* NETANIM_ENABLED */

        void createServers();
        void connectServers();

        tuple<ns3::Ptr<ns3::Node>, uint32_t, ns3::Ptr<ns3::Node>, uint32_t> getLinkInterfaceIndices(
            topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx
        );

    public:
        topology_descriptor_t params;
        ClosTopology(const topology_descriptor_t m_params);
        void createTopology();
        void createLinks();

        #if MPI_ENABLED
        void createCoreMPI();
        #endif
        
        void assignIpsLan();
        void assignIpsNaive();
        void assignServerIps();
        void createFabricInterfaces();

        void setupServerRouting();
        void setupCoreRouting();
        
        // MUST be called before creating interfaces
        void installWcmpStack();

        void doEcmp();
        void enableAggregateBackupPaths();

        void doDisableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);
        void doEnableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);
        void doChangeBandwidth(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr);
        void doChangeDelay(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr);

        void echoBetweenHosts(uint32_t client_host, uint32_t server_host, double interval=0.1);
        void unidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate="2Mbps");
        void bidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate="2Mbps");
        void doAllToAllTcp(uint32_t totalNumberOfServers, string scream_rate);

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

        ns3::Ipv4Address getServerAddress(uint32_t host_idx) const {
            uint32_t server_idx = host_idx % this->params.numServers;
            uint32_t edge_idx = host_idx / this->params.numServers;

            return this->serverInterfaces.at(edge_idx).GetAddress(server_idx);
        }

        ns3::Ptr<ns3::Node> getCore(uint32_t idx) {
            if (idx < (this->params.switchRadix / 2)) {
                return this->coreSwitchesEven.Get(idx);
            }
            return this->coreSwitchesOdd.Get(idx - (this->params.switchRadix / 2));
        }

        ns3::Ptr<ns3::Node> getAggregate(uint32_t pod_num, uint32_t idx) {
            return this->aggSwitches[pod_num].Get(idx);
        }

        ns3::Ptr<ns3::Node> getAggregate(uint32_t  full_idx) {
            uint32_t idx = full_idx % (this->params.switchRadix / 2);
            uint32_t pod_num = full_idx / (this->params.switchRadix / 2);
            return this->getAggregate(pod_num, idx);
        }

        ns3::Ptr<ns3::Node> getEdge(uint32_t pod_num, uint32_t idx) {
            return this->edgeSwitches[pod_num].Get(idx);
        }

        ns3::Ptr<ns3::Node> getEdge(uint32_t  full_idx) {
            uint32_t idx = full_idx % (this->params.switchRadix / 2);
            uint32_t pod_num = full_idx / (this->params.switchRadix / 2);
            return this->getEdge(pod_num, idx);
        }

        ns3::Ptr<ns3::Node> getHost(uint32_t edge_idx, uint32_t host_idx) const {
            return this->servers.at(edge_idx).Get(host_idx);
        }

        ns3::Ptr<ns3::Node> getHost(uint32_t host_idx) const {
            uint32_t idx = host_idx % this->params.numServers;
            uint32_t edge_idx = host_idx / this->params.numServers;
            return this->getHost(edge_idx, idx);
        }

        ns3::Ptr<ns3::Node> getLocalHost(uint32_t edge_idx, uint32_t host_idx) const {
            #if MPI_ENABLED
            if (systemId != getSystemIdOfServer(host_idx)) {
                SWARM_DEBG_ALL("Ignoring request for host " << host_idx << " since its systemId is " << getSystemIdOfServer(host_idx));
                return nullptr;
            }
            #endif

            return getHost(edge_idx, host_idx);
        }

        ns3::Ptr<ns3::Node> getLocalHost(uint32_t host_idx) const {
            #if MPI_ENABLED
            if (systemId != getSystemIdOfServer(host_idx)) {
                SWARM_DEBG_ALL("Ignoring request for host " << host_idx << " since its systemId is " << getSystemIdOfServer(host_idx));
                return nullptr;
            }
            #endif

            return getHost(host_idx);
        }

        pair<uint32_t, uint32_t> getPodAndIndex(uint32_t full_idx) const {
            uint32_t idx = full_idx % (this->params.switchRadix / 2);
            uint32_t pod_num = full_idx / (this->params.switchRadix / 2);
            return make_pair(pod_num, idx);
        }

        void installTcpPacketSinks() {            
            for (uint32_t idx = 0; idx < this->serverApplications.size(); idx++) {
                ns3::Ptr<ns3::Node> ptr = this->getLocalHost(idx);
                if (!ptr)
                    continue;

                ns3::PacketSinkHelper sink("ns3::TcpSocketFactory", ns3::InetSocketAddress(this->getServerAddress(idx), TCP_DISCARD_PORT));
                this->serverApplications[idx].Add(sink.Install(ptr));
            }
        }

        void startApplications(double t_start, double t_finish) {
            for (auto const &container: this->serverApplications) {
                container.Start(ns3::Seconds(t_start));
                container.Stop(ns3::Seconds(t_finish));
            }
        }

        uint32_t getSystemIdOfServer(uint32_t host_idx) const {
            uint32_t edge_idx = host_idx / this->params.numServers;

            return getPodAndIndex(edge_idx).first % systemCount;
        }

        uint16_t getNextPort(uint32_t host_idx) {
            return this->portMap[host_idx]++;
        }

        uint16_t getNextPort(uint32_t pod_num, uint32_t edge_idx, uint32_t server_idx) {
            uint32_t host_idx = (pod_num * this->params.switchRadix/2 + edge_idx) * this->params.numServers + server_idx;
            return this->portMap[host_idx]++;
        }

        void addHostToPortMap(uint32_t host_idx) {
            this->portMap[host_idx] = UDP_DISCARD_PORT;
        }

        void addHostToPortMap(uint32_t pod_num, uint32_t edge_idx, uint32_t server_idx) {
            uint32_t host_idx = (pod_num * this->params.switchRadix/2 + edge_idx) * this->params.numServers + server_idx;
            addHostToPortMap(host_idx);
        }
};

/************************************
 * Function pointer typedefs
************************************/

/**
 * Functions that implement link attribute/state changes.
 * Examples being functions that disable a link or change its bandwidth.
 * 
 * Note: These functions cannot be invoked on remote links (i.e. links that
 * connect two distinct MPI logical processes). We will try to make sure this
 * does not become a problem.
*/
typedef void (*link_attribute_change_func)(ClosTopology*, topology_level, uint32_t, topology_level, uint32_t, const string);
typedef void (*link_state_change_func)(ClosTopology*, topology_level, uint32_t, topology_level, uint32_t);

/************************************
 * Function declarations
************************************/

void logDescriptors(topolgoy_descriptor *topo_params);
void changeBandwidth(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr);
void changeDelay(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr);
void disableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);
void enableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);

uint16_t podLevelMapper(ns3::Ipv4Address dest, const topology_descriptor_t *topo_params);
void closHostFlowDispatcher(host_flow *flow, const ClosTopology *topo);

template<typename... Args> void schedule(double t, link_state_change_func func, Args... args);
template<typename... Args> void schedule(double t, link_attribute_change_func func, Args... args);

void reportTimeProgress(double end);
void reportFlowProgress(FlowScheduler *flowSCheduler);
void DoReportProgress(double end, FlowScheduler *flowSCheduler);

#endif /* SWARM_H */