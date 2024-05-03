#ifndef SCENARIO_SCHEDULER_H
#define SCENARIO_SCHEDULER_H

#include "common.h"
#include <fstream>
#include <string>
#include "ns3/abort.h"

#define LINK_DOWN "LINK_DOWN"
#define LINK_UP "LINK_UP"
#define SET_RATE "SET_BW"
#define SET_DELAY "SET_DELAY"
#define MIGRATE "MIGRATE"
#define SET_WCMP "SET_WCMP"
#define SET_LOSS "SET_LOSS"

using namespace std;

/**
 * This array of function pointers is implemented by whatever will call this 
 * and it can be used to schedule events both during runtime and during
 * startup for the topology itself.
 * 
 * For topology functions, the template type will be the ClosTopology instance
 * but for flow functions, it will be the Flow Scheduler.
*/
template <typename T, typename U>
struct scenario_functions {
    // Topology functions
    void (*set_bw_func) (T*, topology_level, uint32_t, topology_level, uint32_t, const std::string);
    void (*set_delay_func) (T*, topology_level, uint32_t, topology_level, uint32_t, const std::string);
    void (*link_down_func) (T*, topology_level, uint32_t, topology_level, uint32_t, bool);
    void (*link_up_func) (T*, topology_level, uint32_t, topology_level, uint32_t, bool);
    void (*link_loss_func) (T*, topology_level, uint32_t, topology_level, uint32_t, const std::string);
    void (*set_wcmp_func) (T*, topology_level, uint32_t, uint32_t, uint16_t, uint16_t);
    // Flow functions
    void (*migrate_func) (U*, uint32_t, uint32_t, int);
};

template <typename T, typename U>
int parseSecnarioScript(string path, T *topo_object, U *flow_object, scenario_functions<T, U> *scenario_fs) {
    std::ifstream script;
    script.open(path);
    if (script.fail())
        NS_ABORT_MSG("Failed to open the scenario file " << path);

    std::string token;
    std::string type_token;
    while (script >> type_token) {
        SWARM_DEBG("Current token: " << token);
        if (
            !strcmp(type_token.c_str(), LINK_DOWN) || 
            !strcmp(type_token.c_str(), LINK_UP) ||
            !strcmp(type_token.c_str(), SET_RATE) ||
            !strcmp(type_token.c_str(), SET_DELAY) || 
            !strcmp(type_token.c_str(), SET_LOSS)
            ) {

            topology_level level_1, level_2;
            uint32_t index_1, index_2;
            script >> token;
            auto it = topo_level_str2enum.find(token);
            if (it == topo_level_str2enum.end()) {
                SWARM_ERROR("Invalid level: " << token);
                goto fail;
            }
            level_1 = it->second;
            script >> index_1;
            
            script >> token;
            it = topo_level_str2enum.find(token);
            if (it == topo_level_str2enum.end()) {
                SWARM_ERROR("Invalid level: " << token);
                goto fail;
            }
            level_2 = it->second;
            script >> index_2;

            // For link Up/Down, auto-mitigation is done by default
            if (!strcmp(type_token.c_str(), LINK_DOWN)) {
                SWARM_DEBG("Bringing down link between " << level_1 << ":" << index_1 << " and " 
                    << level_2 << ":" << index_2);
                scenario_fs->link_down_func(topo_object, level_1, index_1, level_2, index_2, true);
                continue;
            }
            else if (!strcmp(type_token.c_str(), LINK_UP)) {
                SWARM_DEBG("Bringing up link between " << level_1 << ":" << index_1 << " and " 
                    << level_2 << ":" << index_2);
                scenario_fs->link_up_func(topo_object, level_1, index_1, level_2, index_2, true);
                continue;
            }

            string rate_or_delay;
            script >> rate_or_delay;

            if (!strcmp(type_token.c_str(), SET_RATE)) {
                SWARM_DEBG("Setting link bandwidth between " << level_1 << ":" << index_1 << " and " 
                    << level_2 << ":" << index_2 << " to " << rate_or_delay);
                scenario_fs->set_bw_func(topo_object, level_1, index_1, level_2, index_2, rate_or_delay);
                continue;
            }
            else if (!strcmp(type_token.c_str(), SET_LOSS)) {
                SWARM_DEBG("Setting loss rate between " << level_1 << ":" << index_1 << " and " 
                    << level_2 << ":" << index_2 << " to " << rate_or_delay);
                scenario_fs->link_loss_func(topo_object, level_1, index_1, level_2, index_2, rate_or_delay);
            }
            else {
                SWARM_DEBG("Setting link delay between " << level_1 << ":" << index_1 << " and " 
                    << level_2 << ":" << index_2 << " to " << rate_or_delay);
                scenario_fs->set_delay_func(topo_object, level_1, index_1, level_2, index_2, rate_or_delay);
            }
        }
        else if (!strcmp(type_token.c_str(), SET_WCMP)) {
            topology_level topo_level;
            uint32_t switch_index, if_index;
            uint16_t level, weight;

            script >> token;
            auto it = topo_level_str2enum.find(token);
            if (it == topo_level_str2enum.end()) {
                SWARM_ERROR("Invalid level: " << token);
                goto fail;
            }
            topo_level = it->second;
            script >> switch_index >> if_index >> level >> weight;

            SWARM_DEBG("Setting WCMP weight on switch" << topo_level << ":" << switch_index << " for interface " 
                << if_index << " on level " << level << " to " << weight);
            scenario_fs->set_wcmp_func(topo_object, topo_level, switch_index, if_index, level, weight);
        }
        else if (!strcmp(type_token.c_str(), MIGRATE)) {
            uint32_t migration_src, migration_dst;
            int percent;

            script >> migration_src >> migration_dst >> percent;
            SWARM_DEBG("Migrating " << percent << " percent of traffic from" << migration_src << " to " 
                << migration_dst);
            scenario_fs->migrate_func(flow_object, migration_src, migration_dst, percent);
        }
        else {
            SWARM_ERROR("Invalid expression: " << type_token);
            goto fail;
        }
    }

    // OK!
    script.close();
    return 0;

fail:
    script.close();
    return -1;
}

#endif /* SCENARIO_SCHEDULER_H */