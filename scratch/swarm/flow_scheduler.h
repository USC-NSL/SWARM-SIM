#ifndef FLOW_SCHEDULER_H
#define FLOW_SCHEDULER_H

#include <fstream>
#include <map>
#include <vector>
#include <assert.h>
#include <functional>

using namespace std;


/**
 * A host flow is designated with its source and destination host
 * index (which uniquely gives the IP addresses), when it starts
 * and how big it is.
 * We send the flows in a bulk, an application sends as much data as
 * it can until the full size is reached.
*/
typedef struct host_flow_t {
    uint16_t src, dst;
    double t_arrival;
    uint32_t size;
} host_flow;

/**
 * The `host_flow_dispatcher` is a function that actually creates
 * and starts the application for a flow. This is the function that
 * we schedule for the arrival time of each flow.
 * Our implementation uses a SingleFlowApplication instance.
*/
typedef std::function<void(host_flow_t*)> host_flow_dispatcher;

/**
 * This calss reads a flow file and schedules each flow in a lazy 
 * fashion (i.e. schedule one at a time).
*/
class FlowScheduler {
    private:
        uint32_t current_idx = 0;
        uint32_t num_flows;

        const char *m_flow_file_path;
        ifstream m_flow_file_stream;
        host_flow m_current_flow = {0};
        bool m_enabled = false;

        /**
         * A migration, is another host sending some other hosts traffic on behalf of them.
         * So essentially, if we say that `50 percent of host A traffic is migrated to B`, 
         * we mean that for each flow entry that starts from A, there is 50 percent chance that
         * we will send it from B instead.
        */
        map<uint32_t, map<uint32_t, uint8_t>> m_migrations;
        
        host_flow_dispatcher m_dispatcher = nullptr;

        void readFlowFile();
        void getNextFlow();
        void dispatchAndSchedule();

        uint32_t getMigrationSource(uint32_t original_source) {
            assert(this->m_migrations.count(original_source));

            uint8_t sum = 0;
            uint8_t r = (uint8_t) (rand() % 100);
            for (auto const & elem: this->m_migrations[original_source]) {
                if (r > sum)
                    sum += elem.second;
                else
                    return elem.first;
            }
            return original_source;
        }

    public:
        FlowScheduler(const char *flow_file);
        FlowScheduler(string flow_file);
        FlowScheduler(const char *flow_file, host_flow_dispatcher dispatcher);
        FlowScheduler(string flow_file, host_flow_dispatcher dispatcher);

        void begin();
        void close();
        
        void setDispatcher(host_flow_dispatcher dispatcher) {
            m_dispatcher = dispatcher;
        }

        uint32_t getNumFlows() const {
            return num_flows;
        }

        uint32_t getNumberOfScheduledFlows() const {
            return current_idx;
        }

        void migrateTo(uint32_t original_source, uint32_t migration_destination, uint8_t percent) {
            // Is there are already migrated traffic?
            if (m_migrations.count(original_source)) {
                // Is there already traffic handled by the destination
                if (m_migrations[original_source].count(migration_destination)) {
                    // Add to it!
                    m_migrations[original_source][migration_destination] += percent;
                }
                else
                    m_migrations[original_source][migration_destination] = percent;
            }
            else
                m_migrations[original_source][migration_destination] = percent;
        }

        void migrateBack(uint32_t original_source, uint32_t migration_destination, uint8_t percent) {
            assert(m_migrations[original_source][migration_destination] >= percent);
            
            m_migrations[original_source][migration_destination] -= percent;
            if (m_migrations[original_source][migration_destination] == 0)
                m_migrations[original_source].erase(migration_destination);
        }
};

#endif /* FLOW_SCHEDULER_H */