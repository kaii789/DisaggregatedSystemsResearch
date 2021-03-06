#ifndef __QUEUE_MODEL_NETWORK_LATENCY_ONLY_H__
#define __QUEUE_MODEL_NETWORK_LATENCY_ONLY_H__

#include "queue_model.h"
#include "fixed_types.h"
// #include "contention_model.h"

#include <map>

class QueueModelNetworkLatencyOnly : public QueueModel
{
public:
   QueueModelNetworkLatencyOnly(String name, UInt32 id);
   ~QueueModelNetworkLatencyOnly();

   SubsecondTime computeQueueDelay(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester = INVALID_CORE_ID);
   SubsecondTime computeQueueDelayNoEffect(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester = INVALID_CORE_ID);

private:
   UInt64 m_total_requests;
   SubsecondTime m_total_utilized_time;
   SubsecondTime m_total_queue_delay;

   const SubsecondTime m_r_added_latency; // Additional network latency from remote access

   String m_name;  // temporary, for debugging
};

#endif /* __QUEUE_MODEL_NETWORK_LATENCY_ONLY_H__ */
