#ifndef FLOW_SCHEDULER_H
#define FLOW_SCHEDULER_H

#include <fstream>
#include <functional>

using namespace std;

typedef struct host_flow_t {
    uint16_t src, dst;
    double t_arrival;
    uint32_t size;
} host_flow;

typedef std::function<void(host_flow_t*)> host_flow_dispatcher;

class FlowScheduler {
    private:
        uint32_t current_idx = 0;
        uint32_t num_flows;

        const char *m_flow_file_path;
        ifstream m_flow_file_stream;
        host_flow m_current_flow = {0};
        
        host_flow_dispatcher m_dispatcher = nullptr;

        void readFlowFile();
        void getNextFlow();
        void dispatchAndSchedule();

    public:
        FlowScheduler(const char *flow_file);
        FlowScheduler(string flow_file);
        FlowScheduler(const char *flow_file, host_flow_dispatcher dispatcher);
        FlowScheduler(string flow_file, host_flow_dispatcher dispatcher);

        void begin();
        void setDispatcher(host_flow_dispatcher dispatcher) {
            m_dispatcher = dispatcher;
        }

        uint32_t getNumFlows() const {
            return num_flows;
        }

        uint32_t getNumberOfScheduledFlows() const {
            return current_idx;
        }
};

#endif /* FLOW_SCHEDULER_H */