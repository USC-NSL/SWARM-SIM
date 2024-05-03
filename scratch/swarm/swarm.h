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

#include "scenario_parser.h"
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

/**
 * File outputs
 * 
 * NOTE: For `PCAP_DIR`, the MakeFeile uses a hard-coded value
 *       for it, so keep in mind to update it if you change
 *       the value here.
*/
string ANIM_FILE_OUTPUT = "swarm-anim.xml";
string FLOW_FILE_OUTPUT = "swarm-flow.xml";
string FLOW_FILE_PREFIX = "swarm-flow";
string PCAP_DIR = "swarm-pcaps";
string PCAP_PREFIX = "host-";

#if MPI_ENABLED
#include "ns3/mpi-module.h"
#endif /* MPI_ENABLED */

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
#define TCP_PACKET_SIZE 1024

#define TICK_PROGRESS_EVERY_WHAT_PERCENT 0.1
#define CHECK_FLOW_COMPLETION_EVERY_WHAT_MS 10
#define PROGRESS_BAR_WIDTH 70

#define QUIET_INTERVAL_LENGTH 1.0
#define APPLICATION_START_TIME 1.0

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
 * Host flow dispatcher
*/
host_flow_dispatcher host_flow_dispatcher_function;

/************************************
 *  Simulation inputs
************************************/

double param_end = 4.0;                       // When simulation ends in seconds
double param_monitor_until = 3.0;             // When to stop monitoring new flows?

std::string param_flow_file_path = "";        // Path to traffic file
std::string param_scneario_file_path = "";    // Path to scenario file
std::string param_screamRate = "";            // Rate of All-to-All TCP scream

bool param_micro = false;                     // Use micro-seconds time resolution
bool param_verbose = false;                   // Enable SWARM_DEBUG outputs
bool param_monitor = false;                   // Enable FlowMonitor and FCT reporting
bool param_plain_ecmp = false;                // Do plain ECMP
bool param_use_cache = false;                 // Use ECMP/WCMP cache
bool param_no_acks = false;                   // Do not monitor ACK flows

#if MPI_ENABLED
uint32_t param_pod_procs = DEFAULT_NUM_PODS;  // Number of processes for pod
uint32_t param_core_procs = DEFAULT_NUM_PODS; // Number of processes for core switches
bool param_offlaod_core = false;              // Use separate processes for cores
bool param_offload_aggs = false;              // Use separate processes for aggregates
bool param_first_core_0 = true;
bool param_first_agg_0 = true;
#endif

std::string param_tcp_variant = "TcpDctcp";   // TCP variant to use

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
        // Core switches are fully deployed to make things easier,
        // so with a switch radix of `r`, each core will have exactly
        // one link to each pod, so there are at most `r` pods, each 
        // having r^2/4 uplinks, so we need r^2/4 core switches.
        ns3::NodeContainer coreSwitches;

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
        void createPodMPI();
        #endif
        
        void assignServerIps();
        void createFabricInterfaces();

        void setupServerRouting();
        void setupCoreRouting();
        
        // MUST be called before creating interfaces
        void installWcmpStack();

        void doEcmp();
        void enableAggregateBackupPaths();

        void mitigateEdgeToAggregateLink(uint32_t ei, uint32_t aj, uint16_t weight);
        void mitigateEdgeToAggregateLinkDown(uint32_t ei, uint32_t aj);
        void mitigateEdgeToAggregateLinkUp(uint32_t ei, uint32_t aj);
        void mitigateAggregateToCoreLink(uint32_t ei, uint32_t aj, uint16_t weight);
        void mitigateAggregateToCoreLinkDown(uint32_t ai, uint32_t cj);
        void mitigateAggregateToCoreLinkUp(uint32_t ai, uint32_t cj);
        void restoreStaticRoutesAggregate(uint32_t agg_idx);
        void restoreStaticRoutesCore(uint32_t core_idx);
        
        void mitigateLinkDown(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);
        void mitigateLinkUp(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx);

        void doDisableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, bool auto_mitiagate);
        void doEnableLink(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, bool auto_mitiagate);
        void doChangeBandwidth(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr);
        void doChangeDelay(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr);
        void doUpdateWcmp(topology_level node_level, uint32_t node_idx, uint32_t interface_idx, uint16_t level, uint16_t weight);
        void doSetLinkLoss(topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string packetLossRate);

        void echoBetweenHosts(uint32_t client_host, uint32_t server_host, double interval=0.1);
        void unidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate="2Mbps");
        void bidirectionalCbrBetweenHosts(uint32_t client_host, uint32_t server_host, const string rate="2Mbps");
        void doAllToAllTcp(uint32_t totalNumberOfServers, const string scream_rate);

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
            return this->coreSwitches.Get(idx);
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

        uint32_t getPodNum(uint32_t full_idx) {
            return full_idx / (this->params.switchRadix / 2);
        }

        pair<uint32_t, uint32_t> getPodAndIndex(uint32_t full_idx) const {
            uint32_t idx = full_idx % (this->params.switchRadix / 2);
            uint32_t pod_num = full_idx / (this->params.switchRadix / 2);
            return make_pair(pod_num, idx);
        }

        void installTcpPacketSinks() {            
            for (uint32_t idx = 0; idx < this->params.numPods * this->params.numServers * this->params.switchRadix / 2; idx++) {
                ns3::Ptr<ns3::Node> ptr = this->getLocalHost(idx);
                if (!ptr)
                    continue;

                ns3::PacketSinkHelper sink("ns3::TcpSocketFactory", ns3::InetSocketAddress(this->getServerAddress(idx), TCP_DISCARD_PORT));
                this->serverApplications[idx].Add(sink.Install(ptr));
                this->serverApplications[idx].Start(ns3::Seconds(0));
            }
        }

        void startApplications(double t_start, double t_finish) {
            for (auto const &container: this->serverApplications) {
                container.Start(ns3::Seconds(t_start));
                container.Stop(ns3::Seconds(t_finish));
            }
        }

        uint32_t getSystemIdOfServer(uint32_t host_idx) const {
            if (!this->params.mpi)
                return 0;

            static uint32_t numAggAndEdgeeSwitchesPerPod = this->params.switchRadix / 2;
            static uint32_t sysIdStep = 
                ((numAggAndEdgeeSwitchesPerPod * this->params.numPods) > (param_pod_procs)) ?
                    ((numAggAndEdgeeSwitchesPerPod * this->params.numPods) / (param_pod_procs)) : 1;

            uint32_t edge_idx = host_idx / this->params.numServers;

            return edge_idx / sysIdStep;
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

        void printSystemIds() {
            #if MPI_ENABLED
            SWARM_DEBG("Printing topology system identifiers");
            SWARM_DEBG("We have " << this->coreSwitches.GetN() << " core swithces");
            for (uint32_t i = 0; i < this->coreSwitches.GetN(); i++) {
                SWARM_DEBG("Core " << i << ": " << this->coreSwitches.Get(i)->GetSystemId());
            }

            SWARM_DEBG("We have " << this->aggSwitches.size() << " pods, each with "
                << this->aggSwitches[0].GetN() << " aggregate switches");
            for (uint32_t i = 0; i < this->aggSwitches.size(); i++) {
                for (uint32_t j = 0; j < this->aggSwitches[i].GetN(); j++) {
                    SWARM_DEBG("Aggregate in pod " << i << " and index " << j << " (full index: " 
                        << (this->params.switchRadix/2 * i + j) << "): " << this->aggSwitches[i].Get(j)->GetSystemId());
                }
            }

            SWARM_DEBG("We have " << this->edgeSwitches.size() << " pods, each with "
                << this->edgeSwitches[0].GetN() << " edge switches");
            for (uint32_t i = 0; i < this->edgeSwitches.size(); i++) {
                for (uint32_t j = 0; j < this->edgeSwitches[i].GetN(); j++) {
                    SWARM_DEBG("Edge in pod " << i << " and index " << j << " (full index: " 
                        << (this->params.switchRadix/2 * i + j) << "): " << this->edgeSwitches[i].Get(j)->GetSystemId());
                }
            }

            SWARM_DEBG("We have " << this->servers.size() << " ToRs, each with "
                << this->servers[0].GetN() << " servers");
            for (uint32_t i = 0; i < this->servers.size(); i++) {
                for (uint32_t j = 0; j < this->servers[i].GetN(); j++) {
                    SWARM_DEBG("Server under ToR " << i << " and index " << j << " (full index: " 
                        << (this->params.numServers * i + j) << "): " << this->servers[i].Get(j)->GetSystemId()
                        << " vs " << getSystemIdOfServer(this->params.numServers * i + j));
                }
            }
            #endif
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
typedef void (*link_state_change_func)(ClosTopology*, topology_level, uint32_t, topology_level, uint32_t, bool);
typedef void (*wcmp_update_func)(ClosTopology*, topology_level, uint32_t, uint32_t, uint16_t, uint16_t);
typedef void (*host_traffic_migration_func)(FlowScheduler*, uint32_t, uint32_t, int);

/************************************
 * Function declarations
************************************/

void logDescriptors(topolgoy_descriptor *topo_params);
void changeBandwidth(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string dataRateStr);
void changeDelay(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string delayStr);
void disableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, bool auto_mitigate=false);
void enableLink(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, bool auto_mitigate=false);
void updateWcmp(ClosTopology *topology, topology_level node_level, uint32_t node_idx, uint32_t interface_idx, uint16_t level, uint16_t weight);
void migrateTraffic(FlowScheduler *flow_scheduler, uint32_t migration_source, uint32_t migration_destination, int percent);
void setLossRate(ClosTopology *topology, topology_level src_level, uint32_t src_idx, topology_level dst_level, uint32_t dst_idx, const string packetLossRate);

uint16_t podLevelMapper(ns3::Ipv4Address dest, const topology_descriptor_t *topo_params);
uint16_t torLevelMapper(ns3::Ipv4Address dest, const topology_descriptor_t *topo_params);
void closHostFlowDispatcher(host_flow *flow, const ClosTopology *topo);

template<typename... Args> void schedule(double t, link_state_change_func func, Args... args);
template<typename... Args> void schedule(double t, link_attribute_change_func func, Args... args);

void reportTimeProgress(double end);
void reportFlowProgress(FlowScheduler *flowSCheduler);
void DoReportProgress(double end, FlowScheduler *flowSCheduler);

void bindScenarioFunctions(scenario_functions<ClosTopology, FlowScheduler> *funcs);

void doGlobalConfigs() {
    ns3::Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", ns3::BooleanValue(true));
    ns3::Config::SetDefault("ns3::TcpL4Protocol::SocketType",
        ns3::TypeIdValue(ns3::TypeId::LookupByName("ns3::" + param_tcp_variant)));
}

void parseCmd(int argc, char* argv[], topolgoy_descriptor *topo_params);

uint32_t setupSwarmSimulator(int argc, char* argv[], topology_descriptor_t *topo_params);

void setupClosTopology(ClosTopology *nodes);

template<typename T> 
void setupMonitoringAndBeingExperiment(
    ClosTopology *nodes, 
    uint32_t totalNumberOfServers,
    std::string flow_output_file_name);

#endif /* SWARM_H */