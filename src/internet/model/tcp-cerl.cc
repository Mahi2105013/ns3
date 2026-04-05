#include "tcp-cerl.h"
#include "tcp-cerl-recovery.h"

#include "tcp-socket-state.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TcpCerl");
NS_OBJECT_ENSURE_REGISTERED(TcpCerl);

TypeId
TcpCerl::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TcpCerl")
                            .SetParent<TcpNewReno>()
                            .AddConstructor<TcpCerl>()
                            .SetGroupName("Internet")
                            .AddAttribute("Beta",
                                          "Threshold for congestion detection",
                                          UintegerValue(3),
                                          MakeUintegerAccessor(&TcpCerl::m_beta),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("BottleneckBandwidth",
                                          "Bandwidth of the bottleneck link (B in l=(RTT-T)*B)",
                                          DataRateValue(DataRate("2Mbps")),
                                          MakeDataRateAccessor(&TcpCerl::m_bottleneckBw),
                                          MakeDataRateChecker());
    return tid;
}

TcpCerl::TcpCerl()
    : TcpNewReno(),
    //   m_recovery(CreateObject<TcpCerlRecovery>()),
      m_baseRtt(Time::Max()),
      m_minRtt(Time::Max()),
      m_cntRtt(0),
      m_doingCerlNow(true),
      m_diff(0),
      m_inc(true),
      m_ackCnt(0),
      m_beta(6),
      m_maxQueueLen(0),
      A(0.55),
      m_bottleneckBw(DataRate("2Mbps"))
{
    NS_LOG_FUNCTION(this);
}

TcpCerl::TcpCerl(const TcpCerl& sock)
    : TcpNewReno(sock),
    //   m_recovery(sock.m_recovery),
      m_baseRtt(sock.m_baseRtt),
      m_minRtt(sock.m_minRtt),
      m_cntRtt(sock.m_cntRtt),
      m_doingCerlNow(true),
      m_diff(0),
      m_inc(true),
      m_ackCnt(sock.m_ackCnt),
      m_beta(sock.m_beta),
      m_maxQueueLen(sock.m_maxQueueLen), // l_max
      A(sock.A),
      m_bottleneckBw(sock.m_bottleneckBw)
{
    NS_LOG_FUNCTION(this);
}

TcpCerl::~TcpCerl()
{
    NS_LOG_FUNCTION(this);
}

Ptr<TcpCongestionOps>
TcpCerl::Fork()
{
    return CopyObject<TcpCerl>(this);
}

double 
TcpCerl::ComputeRttVariance()
{
    if (m_rttSamples.size() < 2) return 0.0;
    double mean = 0.0;
    for (const auto& s : m_rttSamples) mean += s.GetSeconds();
    mean /= m_rttSamples.size();
    double var = 0.0;
    for (const auto& s : m_rttSamples)
    {
        double d = s.GetSeconds() - mean;
        var += d * d;
    }
    return var / m_rttSamples.size();
}

// a queue of max capacity 10. Every time we wanna enqueue a new rtt sample, we check if the queue is full. If it is, we dequeue the oldest sample and enqueue the new one. We also update m_minRtt accordingly.
void
TcpCerl::EnqueueRttSample(const Time& rtt)
{
    if (m_rttSamples.size() >= 10)
    {
        m_rttSamples.pop_front();
    }
    m_rttSamples.push_back(rtt);
    
    // Update m_minRtt: track the all-time smallest RTT observed
    // minrtt is the min of the 10 values in the queue:
    m_minRtt = Time::Max();
    for (const auto& sample : m_rttSamples)
    {
        m_minRtt = std::min(m_minRtt, sample);
    }
}

// Sliding window of last 20 L values. Recompute m_cerlLmax as the max of the window.
void
TcpCerl::EnqueueLSample(Ptr<TcpSocketState> tcb, double l)
{
    if (m_lSamples.size() >= 20)
    {
        m_lSamples.pop_front();
    }
    m_lSamples.push_back(l);

    // Recompute Lmax from the sliding window
    tcb->m_cerlLmax = 0.0;
    for (const auto& sample : m_lSamples)
    {
        if (sample > tcb->m_cerlLmax)
        {
            tcb->m_cerlLmax = sample;
        }
    }
    NS_LOG_DEBUG("Updated m_cerlLmax (from last " << m_lSamples.size() << " L samples)= " << tcb->m_cerlLmax);
}

void
TcpCerl::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    // Ptr<TcpRecoveryOps> recovery = tcb->m_recovery;
    // Ptr<TcpCerlRecovery> m_recovery = DynamicCast<TcpCerlRecovery>(recovery);

    std::cout << "TCPCerl here! PktsAcked called with segmentsAcked=" << segmentsAcked << ", rtt=" << rtt << std::endl;

    NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);
    std::cout << "rtt is: " << rtt << std::endl;
    // std::cout << "m_minRtt is: " << m_minRtt << std::endl;

    if (rtt.IsZero())
    {
        return;
    }

    std::cout << "Non Updated m_minRtt= " << m_minRtt << std::endl;

    EnqueueRttSample(rtt);

    // print the entire queue of rtt samples
    std::cout << "Current RTT samples in the queue: ";
    for (const auto& sample : m_rttSamples)
    {
        std::cout << sample << " ";
    }
    std::cout << std::endl;

    // m_minRtt = std::min(m_minRtt, rtt);
    std::cout << "Updated m_minRtt= " << m_minRtt << std::endl;

    // Bottleneck bandwidth B in bytes/sec
    double bw = static_cast<double>(m_bottleneckBw.GetBitRate()) / 8.0;
    std::cout << "Bottleneck bandwidth in bytes/sec: " << bw << std::endl;

    // Queue length estimate 
    // m_recovery->setL((rtt - m_minRtt).GetSeconds() * bw);
    tcb->m_cerlL = (rtt - m_minRtt).GetSeconds() * bw;

    // Track max queue length over last 20 L samples
    EnqueueLSample(tcb, tcb->m_cerlL);

    // update N
    tcb->m_cerlN = A * tcb->m_cerlLmax;

    // std::cout << "wow!!" << A * tcb->m_cerlLmax << std::endl;
    // print minRTT, L, Lmax, N:
    std::cout << "WOW!! minRtt: " << m_minRtt << ", L: " << tcb->m_cerlL << ", Lmax: " << tcb->m_cerlLmax << ", N: " << tcb->m_cerlN << std::endl;

    std::cout << "Updated tcb->m_cerlLmax= " << tcb->m_cerlLmax << std::endl;

    // no need for any base rtt
    m_baseRtt = std::min(m_baseRtt, rtt);
    NS_LOG_DEBUG("Updated m_baseRtt= " << m_baseRtt);

    // Update RTT counter
    // m_cntRtt++;
    m_cntRtt = m_rttSamples.size();
    NS_LOG_DEBUG("Updated m_cntRtt= " << m_cntRtt);
    std::cout << "Updated m_cntRtt= " << m_cntRtt << std::endl;

    // Update A based on RTT variance
    double rttVar = ComputeRttVariance();
    double normalizedVar = rttVar / (m_minRtt.GetSeconds() * m_minRtt.GetSeconds() + 1e-12);
    A = std::max(0.2, std::min(0.8, 0.55 - 0.3 * normalizedVar));

    std::cout << "Updated A based on RTT variance: " << A << std::endl;
}

void
TcpCerl::EnableCerl()
{
    NS_LOG_FUNCTION(this);

    m_doingCerlNow = true;
    // Do NOT reset m_minRtt here — it is derived from the sliding window of last 10 RTT samples
}

void
TcpCerl::DisableCerl()
{
    NS_LOG_FUNCTION(this);

    m_doingCerlNow = false;
}

void
TcpCerl::CongestionStateSet(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_FUNCTION(this << tcb << newState);
    if (newState == TcpSocketState::CA_OPEN)
    /*
    CA_OPEN means the TCP connection is:
    - Fully established
    - Not recovering from packet loss
    - Not in slow start, rather in congestion avoidance
    - Not panicking about congestion
    - Increasing its congestion window normally, using the congestion-avoidance algorithm    
    */
    {
        EnableCerl();
        NS_LOG_LOGIC("Cerl is now on.");
    }
    else
    {
        // DisableCerl();
        // NS_LOG_LOGIC("Cerl is turned off.");
    }
}

void
TcpCerl::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    // Always calculate m_diff, even if we are not doing Cerl now
    uint32_t targetCwnd;
    uint32_t segCwnd = tcb->GetCwndInSegments();

    /*
     * Calculate the cwnd we should have. baseRtt is the minimum RTT
     * per-connection, minRtt is the minimum RTT in this window
     *
     * little trick:
     * desidered throughput is currentCwnd * baseRtt
     * target cwnd is throughput / minRtt
     */
    double tmp = m_baseRtt.GetSeconds() / m_minRtt.GetSeconds();
    targetCwnd = static_cast<uint32_t>(segCwnd * tmp);
    NS_LOG_DEBUG("Calculated targetCwnd = " << targetCwnd);
    NS_ASSERT(segCwnd >= targetCwnd); // implies baseRtt <= minRtt

    // Calculate the difference between actual and target cwnd
    m_diff = segCwnd - targetCwnd;
    NS_LOG_DEBUG("Calculated m_diff = " << m_diff);

    if (!m_doingCerlNow)
    {
        // If Cerl is not on, we follow NewReno algorithm
        NS_LOG_LOGIC("Cerl is not turned on, we follow NewReno algorithm.");
        std::cout << "Cerl is not turned on, we follow NewReno algorithm." << std::endl;
        TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
        return;
    }

    // We do the Cerl calculations only if we got enough RTT samples
    if (m_cntRtt <= 2)
    { // We do not have enough RTT samples, so we should behave like NewReno
        NS_LOG_LOGIC("We do not have enough RTT samples to perform Cerl "
                     "calculations, we behave like NewReno.");
        std::cout << "We do not have enough RTT samples to perform Cerl "
                     "calculations, we behave like NewReno." << std::endl;
        TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
    }
    else
    {
        NS_LOG_LOGIC("We have enough RTT samples to perform Cerl calculations.");
        std::cout << "We have enough RTT samples to perform Cerl calculations." << std::endl;

        if (tcb->m_cWnd < tcb->m_ssThresh)
        { // Slow start mode. Cerl employs same slow start algorithm as NewReno's.
            NS_LOG_LOGIC("We are in slow start, behave like NewReno.");
            TcpNewReno::SlowStart(tcb, segmentsAcked);
        }
        else
        { // Congestion avoidance mode
            NS_LOG_LOGIC("We are in congestion avoidance, execute Cerl additive "
                         "increase algo.");

            if (m_diff < m_beta)
            {
                // Available bandwidth is not fully utilized,
                // increase cwnd by 1 every RTT
                NS_LOG_LOGIC("Available bandwidth not fully utilized, increase "
                             "cwnd by 1 every RTT");
                TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);
            }
            else
            {
                // Available bandwidth is fully utilized,
                // increase cwnd by 1 every other RTT
                NS_LOG_LOGIC("Available bandwidth fully utilized, increase cwnd "
                             "by 1 every other RTT");
                if (m_inc)
                {
                    TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);
                    m_inc = false;
                }
                else
                {
                    m_inc = true;
                }
            }
        }
    }

    // Reset cntRtt every RTT (m_minRtt is the all-time minimum, never reset)
    // m_cntRtt = 0;
}

std::string
TcpCerl::GetName() const
{
    return "TcpCerl";
}

// bool
// TcpCerl::IsCongestiveLoss()
// {
//     // return l greater than or equal to N or not
//     return (m_recovery->getL() >= m_recovery->getN());
// }


uint32_t
TcpCerl::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << tcb << bytesInFlight);
    return TcpNewReno::GetSsThresh(tcb, bytesInFlight);

    // // if (m_diff < m_beta)
    // if (l < N)
    // {
    //     // random loss due to bit errors is most likely to have occurred,
    //     // we cut cwnd by 1/5
    //     NS_LOG_LOGIC("Random loss is most likely to have occurred, "
    //                  "cwnd is reduced by nothing at all!");
    //     return std::max(static_cast<uint32_t>(bytesInFlight), 2 * tcb->m_segmentSize);
    // }
    // else
    // {
    //     // congestion-based loss is most likely to have occurred,
    //     // we reduce cwnd by 1/2 as in NewReno
    //     NS_LOG_LOGIC("Congestive loss is most likely to have occurred, "
    //                  "cwnd is halved");
    //     return TcpNewReno::GetSsThresh(tcb, bytesInFlight);
    // }
}

} // namespace ns3
