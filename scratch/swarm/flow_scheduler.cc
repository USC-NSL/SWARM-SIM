#include "flow_scheduler.h"
#include "ns3/simulator.h"


using namespace ns3;


FlowScheduler :: FlowScheduler(const char *flow_file) 
    : m_flow_file_path(flow_file)
{
    m_flow_file_stream.open(m_flow_file_path);
}

FlowScheduler :: FlowScheduler(string flow_file) 
    : m_flow_file_path(flow_file.c_str())
{
    m_flow_file_stream.open(m_flow_file_path);
}

FlowScheduler :: FlowScheduler(const char *flow_file, host_flow_dispatcher dispatcher) 
    : m_flow_file_path(flow_file),
      m_dispatcher(dispatcher)
{
    m_flow_file_stream.open(m_flow_file_path);
}

FlowScheduler :: FlowScheduler(string flow_file, host_flow_dispatcher dispatcher) 
    : m_flow_file_path(flow_file.c_str()),
      m_dispatcher(dispatcher)
{
    m_flow_file_stream.open(m_flow_file_path);
}

void
FlowScheduler :: getNextFlow() {
    m_flow_file_stream 
        >> m_current_flow.src 
        >> m_current_flow.dst 
        >> m_current_flow.size 
        >> m_current_flow.t_arrival;
}

void
FlowScheduler :: dispatchAndSchedule() {
    NS_ASSERT(m_dispatcher);
    NS_ASSERT(m_current_flow.t_arrival > 0);

    if (current_idx < num_flows) {
        // Wait if it is not yet time
        if (m_current_flow.t_arrival > Simulator::Now().GetDouble()) {
            goto schedule_next;
        }

        // Call the dispatcher
        m_dispatcher(&m_current_flow);

        // Get next flow
        getNextFlow();
        current_idx++;
    }
    else {
        m_flow_file_stream.close();
        return;
    }

schedule_next:
    Simulator::Schedule(
        Seconds(m_current_flow.t_arrival) - Simulator::Now(),
        &FlowScheduler::dispatchAndSchedule, this
    );
}

void 
FlowScheduler :: begin() {
    getNextFlow();
    dispatchAndSchedule();
}
