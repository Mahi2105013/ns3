#ifndef TCPCERL_H
#define TCPCERL_H

#include "tcp-congestion-ops.h"
#include "tcp-cerl-recovery.h"

#include "ns3/data-rate.h"

#include <deque>

namespace ns3
{
class TcpCerlRecovery; // forward declaration

class TcpSocketState;

/**
 * @ingroup congestionOps
 * */

class TcpCerl : public TcpNewReno
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * Create an unbound tcp socket.
     */
    TcpCerl();

    /**
     * @brief Copy constructor
     * @param sock the object to copy
     */
    TcpCerl(const TcpCerl& sock);
    ~TcpCerl() override;

    std::string GetName() const override;

    /**
     * @brief Perform RTT sampling needed to execute Veno algorithm
     *
     * The function filters RTT samples from the last RTT to find
     * the current smallest propagation delay + queueing delay (m_minRtt).
     * We take the minimum to avoid the effects of delayed ACKs.
     *
     * The function also min-filters all RTT measurements seen to find the
     * propagation delay (m_baseRtt).
     *
     * @param tcb internal congestion state
     * @param segmentsAcked count of segments ACKed
     * @param rtt last RTT
     *
     */
    void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;

    /**
     * @brief Enable/disable Cerl depending on the congestion state
     *
     * We only start a Cerl when we are in normal congestion state (CA_OPEN state).
     *
     * @param tcb internal congestion state
     * @param newState new congestion state to which the TCP is going to switch
     */
    void CongestionStateSet(Ptr<TcpSocketState> tcb,
                            const TcpSocketState::TcpCongState_t newState) override;

    /**
     * @brief Adjust cwnd following Cerl additive increase algorithm
     *
     * @param tcb internal congestion state
     * @param segmentsAcked count of segments ACKed
     */
    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;

    /**
     * @brief Get slow start threshold during Cerl  multiplicative-decrease phase
     *
     * @param tcb internal congestion state
     * @param bytesInFlight bytes in flight
     *
     * @return the slow start threshold value
     */
    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;

    Ptr<TcpCongestionOps> Fork() override;

  private:
    /**
     * @brief Enable Veno algorithm to start Veno sampling
     *
     * Veno algorithm is enabled in the following situations:
     * 1. at the establishment of a connection
     * 2. after an RTO
     * 3. after fast recovery
     * 4. when an idle connection is restarted
     *
     */
    void EnableCerl();

    /**
     * @brief Turn off Cerl
     */
    void DisableCerl();

    bool IsCongestiveLoss();

  private:
    // Ptr<TcpCerlRecovery> m_recovery; //!< Recovery object used to manage the recovery phase of Cerl
    Time m_baseRtt;      //!< Minimum of all RTT measurements seen during connection
    Time m_minRtt;       //!< Minimum of RTTs measured within last RTT
    uint32_t m_cntRtt;   //!< Number of RTT measurements during last RTT
    bool m_doingCerlNow; //!< If true, do Cerl for this RTT
    uint32_t m_diff;     //!< Difference between expected and actual throughput
    bool m_inc;          //!< If true, cwnd needs to be incremented
    uint32_t m_ackCnt;   //!< Number of received ACK
    uint32_t m_beta;     //!< Threshold for congestion detection
    uint32_t m_maxQueueLen; //!< Maximum queue length at the bottleneck link
    double A;              //!< Parameter for Cerl additive increase algorithm
    DataRate m_bottleneckBw; //!< Bottleneck link bandwidth (B in the CERL formula)
    bool m_modifications; //!< If true, use adaptive A + sliding RTT/L windows; if false, use fixed/global baseline
    std::deque<Time> m_rttSamples; //!< Queue of recent RTT samples
    std::deque<double> m_lSamples; //!< Sliding window of recent m_cerlL values (max 20)

    void EnqueueRttSample(const Time& rtt); //!< Enqueue an RTT sample and update m_minRtt
    void EnqueueLSample(Ptr<TcpSocketState> tcb, double l); //!< Enqueue an L sample and update m_cerlLmax from last 20 values
    double ComputeRttVariance(); //!< Compute variance of RTT samples in the sliding window
};

} // namespace ns3

#endif // TCPCERL_H
