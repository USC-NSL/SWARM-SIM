#include "flow_scheduler.h"
#include "ns3/simulator.h"


using namespace ns3;


FlowScheduler :: FlowScheduler(const char *flow_file) 
    : m_flow_file_path(flow_file)
{
    readFlowFile();
}

FlowScheduler :: FlowScheduler(string flow_file) 
    : m_flow_file_path(flow_file.c_str())
{
    readFlowFile();
}

FlowScheduler :: FlowScheduler(const char *flow_file, host_flow_dispatcher dispatcher) 
    : m_flow_file_path(flow_file),
      m_dispatcher(dispatcher)
{
    readFlowFile();
}

FlowScheduler :: FlowScheduler(string flow_file, host_flow_dispatcher dispatcher) 
    : m_flow_file_path(flow_file.c_str()),
      m_dispatcher(dispatcher)
{
    readFlowFile();
}

void
FlowScheduler :: getNextFlow() {
    m_flow_file_stream 
        >> m_current_flow.src 
        >> m_current_flow.dst 
        >> m_current_flow.size 
        >> m_current_flow.t_arrival;
    
    // Should we migrate traffic?
    if (m_migrations.count(m_current_flow.src)) {
        m_current_flow.dst = getMigrationSource(m_current_flow.src);
    }
    // std::clog << "Current time is " << m_current_flow.t_arrival << "\n";
}

void
FlowScheduler :: dispatchAndSchedule() {
    NS_ASSERT(m_dispatcher);
    // NS_ASSERT(m_current_flow.t_arrival > 0);

    // std::clog << "Current index is  " << current_idx << "\n";

    while (current_idx < num_flows && Seconds(m_current_flow.t_arrival) == Simulator::Now()) {
        // Call the dispatcher
        m_dispatcher(&m_current_flow);

        // Get next flow
        getNextFlow();
        current_idx++;
    }

    if (current_idx < num_flows)
        Simulator::Schedule(
            Seconds(m_current_flow.t_arrival) - Simulator::Now(),
            &FlowScheduler::dispatchAndSchedule, this
        );
    else {
        m_flow_file_stream.close();
        m_enabled = false;
    }
}

void
FlowScheduler :: readFlowFile() {
    m_flow_file_stream.open(m_flow_file_path);
    if (m_flow_file_stream.fail())
        NS_ABORT_MSG("Failed to open flow file at " << std::string(m_flow_file_path));

    m_flow_file_stream >> num_flows;
    m_enabled = true;

    // SWARM_INFO("Reading flow file at " << m_flow_file_path << " with " << num_flows << " flows");
}

void 
FlowScheduler :: begin() {
    getNextFlow();
    Simulator::Schedule(
        Seconds(m_current_flow.t_arrival) - Simulator::Now(),
        &FlowScheduler::dispatchAndSchedule, this
    );
}

void
FlowScheduler :: close() {
    if (m_enabled)
        m_flow_file_stream.close();
}
