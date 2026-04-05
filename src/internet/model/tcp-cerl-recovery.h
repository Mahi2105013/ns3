#ifndef TCP_CERL_RECOVERY_H
#define TCP_CERL_RECOVERY_H

#include "ns3/object.h"
#include "ns3/log.h"

#include "tcp-recovery-ops.h"
#include "tcp-socket-state.h"

namespace ns3 {
  

class TcpCerlRecovery : public TcpRecoveryOps
{
public:
  static TypeId GetTypeId (void);

  TcpCerlRecovery ();

  Ptr<TcpRecoveryOps> Fork () override;

  void EnterRecovery (Ptr<TcpSocketState> tcb,
                      uint32_t dupAckCount,
                      uint32_t unAckDataCount,
                      uint32_t deliveredBytes) override;

  void DoRecovery (Ptr<TcpSocketState> tcb,
                   uint32_t deliveredBytes,
                   bool isDupAck) override;

  void ExitRecovery (Ptr<TcpSocketState> tcb) override;

  std::string GetName() const override;

//   double getL()
//   {
//       return l;
//   }

//   double getN()
//   {
//       return N;
//   }

//   void setL(double l)
//   {
//       this->l = l;
//   }

//   void setN(double n)
//   {
//       this->N = n;
//   }

private:
  uint32_t m_oldCwnd;
  SequenceNumber32 m_lastDecMaxSentSeqno;
//   double l;          // queue length estimate
//   double N;          // threshold
};

} // namespace ns3

#endif /* TCP_CERL_RECOVERY_H */
