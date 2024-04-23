#ifndef SCENARIO_SCHEDULER_H
#define SCENARIO_SCHEDULER_H

#include "common.h"

#define LINK_DOWN "LINK_DOWN"
#define LINK_UP "LINK_UP"
#define SET_RATE "SET_BW"
#define SET_DELAY "SET_DELAY"
#define MIGRATE "MIGRATE"
#define SET_WCMP "SET_WCMP"


template <typename T>
struct topology_funcs {
    void (*set_bw_func) (T*, topology_level, uint32_t, topology_level, uint32_t, const std::string);
    void (*set_delay_func) (T*, topology_level, uint32_t, topology_level, uint32_t, const std::string);
    void (*link_down_func) (T*, topology_level, uint32_t, topology_level, uint32_t, bool);
    void (*link_up_func) (T*, topology_level, uint32_t, topology_level, uint32_t, bool);
    void (*set_wcmp_func) (T*, topology_level, uint32_t, uint32_t, uint16_t, uint16_t);
};


template <typename T>
struct flow_funcs {
    void (*migrate_func) (T*, uint32_t, uint32_t, int);
};


void parseSecnarioScript(const char *path) {
    
}

#endif /* SCENARIO_SCHEDULER_H */