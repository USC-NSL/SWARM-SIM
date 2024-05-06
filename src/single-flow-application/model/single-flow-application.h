#ifndef SINGLE_FLOW_APPLICATION_H
#define SINGLE_FLOW_APPLICATION_H


#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/socket.h"


namespace ns3
{
  
class Address;
class Socket;

class SingleFlowApplication : public Application
{
  public:
    static TypeId GetTypeId();

    SingleFlowApplication();

    ~SingleFlowApplication() override;
    
    void SetFlowSize(uint64_t flowSize);
    void SetNode(Ptr<Node> node);
    void SetAppId(uint32_t appid);

    bool m_reportDone = false;
    bool IsDone() {
      return m_isDone;
    }

    Ptr<Socket> GetSocket() const;

  protected:
    void DoDispose() override;

  private:
    // inherited from Application base class.
    void StartApplication() override; // Called at time specified by Start
    void StopApplication() override;  // Called at time specified by Stop

    // helpers
    void CancelEvents();

    // Event handlers
    void StartSending();
    void StopSending();
    void SendPacket();

    Ptr<Socket> m_socket;                //!< Associated socket
    Ptr<Node> m_node;                    //!< Associated Node of this application
    uint32_t m_appId;                    //!< Unique ID of this application instance
    Address m_peer;                      //!< Peer address
    Address m_local;                     //!< Local address to bind to
    bool m_connected;                    //!< True if connected
    DataRate m_cbrRate;                  //!< Rate that data is generated
    DataRate m_cbrRateFailSafe;          //!< Rate that data is generated (check copy)
    uint32_t m_pktSize;                  //!< Size of packets
    uint32_t m_residualBits;             //!< Number of generated, but not sent, bits
    Time m_lastStartTime;                //!< Time last packet sent
    uint64_t m_flowSize;                 //!< Limit total number of bytes sent
    uint64_t m_totBytes;                 //!< Total bytes sent so far
    EventId m_startStopEvent;            //!< Event id for next start or stop event
    EventId m_sendEvent;                 //!< Event id of pending "send packet" event
    TypeId m_tid;                        //!< Type of the socket used
    uint32_t m_seq{0};                   //!< Sequence
    Ptr<Packet> m_unsentPacket;          //!< Unsent packet cached for future attempt

    /// Traced Callback: transmitted packets.
    TracedCallback<Ptr<const Packet>> m_txTrace;

    /// Callbacks for tracing the packet Tx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_txTraceWithAddresses;

    bool m_isDone = false;

  private:
    void ScheduleNextTx();
    void ScheduleStartEvent();
    void ConnectionSucceeded(Ptr<Socket> socket);
    void ConnectionFailed(Ptr<Socket> socket);
};

} // namespace ns3

#endif /* SINGLE_FLOW_APPLICATION_H */
