#ifndef __QUEUE_MODEL_H__
#define __QUEUE_MODEL_H__

#include "fixed_types.h"
#include "subsecond_time.h"

#include <iostream>

#include <vector>  // for computeQueueDelayTrackBytesPotentialPushback()

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
   virtual SubsecondTime computeQueueDelayTrackBytes(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, core_id_t requester = INVALID_CORE_ID, bool is_inflight_page = false, UInt64 phys_page = 0) {
      return computeQueueDelay(pkt_time, processing_time, requester);  // the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   };
   virtual SubsecondTime computeQueueDelayTrackBytesPotentialPushback(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, std::vector<std::pair<UInt64, SubsecondTime>>& new_inflight_page_arrival_time_deltas, bool compute_inflight_page_delays = false, core_id_t requester = INVALID_CORE_ID) {
      return computeQueueDelayTrackBytes(pkt_time, processing_time, num_bytes, request_type, requester);  // placeholder since the method is currently only implemented in the windowed_mg1_remote_subqueuemodels QueueModel
   };

   virtual SubsecondTime computeQueueDelayAfterAddNoEffect(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester = INVALID_CORE_ID) {
      return SubsecondTime::Zero(); // the method is currently only implemented in the windowed_mg1_remote QueueModel
   };

   // virtual SubsecondTime computeQueueDelayTrackBytesInflightPage(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, core_id_t requester = INVALID_CORE_ID, bool is_inflight_page = false, UInt64 phys_page = 0) {
   //    return computeQueueDelayTrackBytes(pkt_time, processing_time, num_bytes, request_type, requester);  // placeholder since the method is currently only implemented in the windowed_mg1_remote_subqueuemodels QueueModel
   // };
   virtual void removeInflightPage(UInt64 phys_page) {
      // placeholder since the method is currently only implemented in the windowed_mg1_remote_subqueuemodels QueueModel
   };

   virtual double getTotalQueueUtilizationPercentage(SubsecondTime pkt_time) {
      return 0.0;  // placeholder since the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   };
   virtual double getPageQueueUtilizationPercentage(SubsecondTime pkt_time) {
      return 0.0;  // placeholder since the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   };
   virtual double getCachelineQueueUtilizationPercentage(SubsecondTime pkt_time) {
      return 0.0;  // placeholder since the method is currently only implemented in the windowed_mg1_remote* QueueModel's
   };

   // This method currently only implemented in remote_ind_queues QueueModel
   virtual void updateBandwidth(UInt64 bw_bits_per_us, double r_cacheline_queue_fraction) {};

   virtual void updateAddedNetLat(int added_latency_ns) {};

   virtual void finalizeStats() {};

   static QueueModel* create(String name, UInt32 id, String model_type, SubsecondTime min_processing_time);
   // Second option to create a queue model that knows its supposed bandwidth (only the windowed_mg1_remote* QueueModel's actually uses this)
   // To create a windowed_mg1_remote or windowed_mg1_remote_combined queue model, must use this second create method
   // static QueueModel* create(String name, UInt32 id, String model_type, SubsecondTime min_processing_time, UInt64 bw_bits_per_us);
   static QueueModel* create(String name, UInt32 id, String model_type, SubsecondTime min_processing_time, UInt64 bw_bits_per_us, SubsecondTime baseline_page_processing_time = SubsecondTime::Zero(), SubsecondTime baseline_cacheline_processing_time = SubsecondTime::Zero());
};

#endif /* __QUEUE_MODEL_H__ */
