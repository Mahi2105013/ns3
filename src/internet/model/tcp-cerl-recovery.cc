#include "tcp-cerl-recovery.h"

#include "ns3/log.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpCerlRecovery");
NS_OBJECT_ENSURE_REGISTERED (TcpCerlRecovery);

TypeId
TcpCerlRecovery::GetTypeId (void)
{
  static TypeId tid =
    TypeId ("ns3::TcpCerlRecovery")
      .SetParent<TcpRecoveryOps> ()
      .SetGroupName ("Internet")
      .AddConstructor<TcpCerlRecovery> ();
  return tid;
}

TcpCerlRecovery::TcpCerlRecovery ()
  : m_oldCwnd (0),
    m_lastDecMaxSentSeqno (0)
    // l (0),
    // N (0)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpRecoveryOps>
TcpCerlRecovery::Fork ()
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpCerlRecovery> (this);
}

void
TcpCerlRecovery::EnterRecovery (Ptr<TcpSocketState> tcb,
                                uint32_t dupAckCount,
                                uint32_t unAckDataCount,
                                uint32_t deliveredBytes)
{
  NS_LOG_FUNCTION (this << tcb << dupAckCount << unAckDataCount << deliveredBytes);
  std::cout << "TcpCerlRecovery: EnterRecovery called with dupAckCount=" << dupAckCount
            << ", unAckDataCount=" << unAckDataCount
            << ", deliveredBytes=" << deliveredBytes << std::endl;
  std::cout << tcb->m_cerlN << " " << tcb->m_cerlL << " " << tcb->m_cerlLmax << std::endl;
  
  // l++;


  SequenceNumber32 highestAck = tcb->m_lastAckedSeq;

  if (tcb->m_cerlL >= tcb->m_cerlN &&
      highestAck > m_lastDecMaxSentSeqno)
    {
      std::cout << "TcpCerlRecovery: Congestive loss detected, performing reduction" << std::endl;
      // TRUE CERL REDUCTION
    //   uint32_t awnd = tcb->m_rcvWnd.Get ();
      uint32_t newSsthresh =
        //   std::min (tcb->m_cWnd.Get (), awnd) / 2;
          tcb->m_cWnd.Get () / 2;

      if (newSsthresh < 2 * tcb->m_segmentSize)
        {
          newSsthresh = 2 * tcb->m_segmentSize;
        }

      tcb->m_ssThresh = newSsthresh;
      tcb->m_cWnd = newSsthresh + 3 * tcb->m_segmentSize;

      m_lastDecMaxSentSeqno = tcb->m_highTxMark;
    }
  else
    {
      // NO REDUCTION CASE
      std::cout << "TcpCerlRecovery: Random loss detected, no reduction performed" << std::endl;
      m_oldCwnd = tcb->m_cWnd;
      tcb->m_cWnd += 3 * tcb->m_segmentSize;
    }
}

// void
// TcpCerlRecovery::DoRecovery (Ptr<TcpSocketState> tcb,
//                              uint32_t deliveredBytes,
//                              bool isDupAck)
// {
//   NS_LOG_FUNCTION (this << tcb << deliveredBytes << isDupAck);

//   // During recovery, allow limited cwnd inflation on new data
//   if (!isDupAck)
//     {
//       tcb->m_cWnd += tcb->m_segmentSize;
//     }
// }

void
TcpCerlRecovery::ExitRecovery (Ptr<TcpSocketState> tcb)
{
  std::cout << "TcpCerlRecovery: ExitRecovery called" << std::endl;
  NS_LOG_FUNCTION (this << tcb);

    if (m_oldCwnd > 0)
    {
        tcb->m_cWnd = m_oldCwnd;
        m_oldCwnd = 0;
    }
    else
    {
        tcb->m_cWnd = tcb->m_ssThresh;
    }

}

std::string TcpCerlRecovery::GetName() const
{
    return "TcpCerlRecovery";
}

void TcpCerlRecovery::DoRecovery(Ptr<TcpSocketState> tcb,
                                 uint32_t deliveredBytes,
                                 bool isDupAck)
{
    // simple cwnd inflation like NewReno does
    std::cout << "TcpCerlRecovery: DoRecovery" << std::endl;
    if (!isDupAck)
    {
        tcb->m_cWnd += tcb->m_segmentSize;
    }
}


} // namespace ns3
