#ifndef __QUEUE_MODEL_H__
#define __QUEUE_MODEL_H__

#include "fixed_types.h"
#include "subsecond_time.h"

#include <iostream>

class QueueModel
{
public:
   // Queue delay request types
   typedef enum
   {
      PAGE = 0,
      CACHELINE,
      NUM_REQUEST_TYPES
   } request_t;

   QueueModel() {}
   virtual ~QueueModel() {}

   virtual SubsecondTime computeQueueDelay(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester = INVALID_CORE_ID) = 0;
   virtual SubsecondTime computeQueueDelayNoEffect(SubsecondTime pkt_time, SubsecondTime processing_time, request_t request_type, core_id_t requester = INVALID_CORE_ID) {
      return SubsecondTime::Zero();  // placeholder since the method is currently only implemented in the windowed_mg1* QueueModel's
   };
   virtual SubsecondTime computeQueueDelayTrackBytes(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, core_id_t requester = INVALID_CORE_ID) {
      return computeQueueDelay(pkt_time, processing_time, requester);  // the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   };

   virtual double getTotalQueueUtilizationPercentage(SubsecondTime pkt_time) {
      return 0.0;  // placeholder since the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   }
   virtual double getPageQueueUtilizationPercentage(SubsecondTime pkt_time) {
      return 0.0;  // placeholder since the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   }

   static QueueModel* create(String name, UInt32 id, String model_type, SubsecondTime min_processing_time);
   // Second option to create a queue model that knows its supposed bandwidth (only the windowed_mg1_remote* QueueModel's actually uses this)
   // To create a windowed_mg1_remote or windowed_mg1_remote_combined queue model, must use this second create method
   // static QueueModel* create(String name, UInt32 id, String model_type, SubsecondTime min_processing_time, UInt64 bw_bits_per_us);
   static QueueModel* create(String name, UInt32 id, String model_type, SubsecondTime min_processing_time, UInt64 bw_bits_per_us, SubsecondTime baseline_page_processing_time = SubsecondTime::Zero(), SubsecondTime baseline_cacheline_processing_time = SubsecondTime::Zero());
};

#endif /* __QUEUE_MODEL_H__ */
