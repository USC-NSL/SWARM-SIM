#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/**
 * When using MPI:
 *  - systemId is the rank of the current process
 *  - systemCount is the number of LPs
*/
uint32_t systemId = 0;
uint32_t systemCount = 1;

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


#endif /* COMMON_H */