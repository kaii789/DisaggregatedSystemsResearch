#ifndef __QUEUE_MODEL_WINDOWED_MG1_REMOTE_IND_QUEUES_H__
#define __QUEUE_MODEL_WINDOWED_MG1_REMOTE_IND_QUEUES_H__

#include "queue_model.h"
#include "fixed_types.h"
#include "contention_model.h"

#include <map>
#include <set>
#include <vector>

/* Putting two independent remote queues together for easier stats keeping */
class QueueModelWindowedMG1RemoteIndQueues : public QueueModel
{
public:
   QueueModelWindowedMG1RemoteIndQueues(String name, UInt32 id, UInt64 bw_bits_per_us);
   ~QueueModelWindowedMG1RemoteIndQueues();

   SubsecondTime computeQueueDelay(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester = INVALID_CORE_ID);
   SubsecondTime computeQueueDelayNoEffect(SubsecondTime pkt_time, SubsecondTime processing_time, request_t request_type, core_id_t requester = INVALID_CORE_ID);

   SubsecondTime computeQueueDelayTrackBytes(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, core_id_t requester = INVALID_CORE_ID, bool is_inflight_page = false, UInt64 phys_page = 0);

   double getTotalQueueUtilizationPercentage(SubsecondTime pkt_time);

   double getPageQueueUtilizationPercentage(SubsecondTime pkt_time);
   double getCachelineQueueUtilizationPercentage(SubsecondTime pkt_time);

   void finalizeStats();

   void updateBandwidth(UInt64 bw_bits_per_us, double r_cacheline_queue_fraction);
   void updateAddedNetLat(int added_latency_ns);

private:
   const SubsecondTime m_window_size;
   SubsecondTime m_queue_delay_cap;      // Only considered if m_use_separate_queue_delay_cap is true; cap applied before network additional latency
   bool m_use_separate_queue_delay_cap;  // Whether to use a separate queue delay cap

   bool m_use_utilization_overflow;
   double m_utilization_overflow_threshold;
   UInt64 m_page_queue_overflowed_to_cacheline_queue;
   UInt64 m_cacheline_queue_overflowed_to_page_queue;

   std::multimap<SubsecondTime, SubsecondTime> m_window_page_requests;
   std::multimap<SubsecondTime, SubsecondTime> m_window_cacheline_requests;
   UInt64 m_num_page_arrivals;
   UInt64 m_num_cacheline_arrivals;
   UInt64 m_page_service_time_sum;       // In ps
   UInt64 m_cacheline_service_time_sum;  // In ps
   UInt64 m_page_service_time_sum2;      // In ps^2
   UInt64 m_cacheline_service_time_sum2; // In ps^2

   SubsecondTime m_r_added_latency;                // Additional network latency from remote access
   const UInt32 m_r_partition_queues;              // Whether partitioned queues is enabled
   double m_r_cacheline_queue_fraction;            // The fraction of remote bandwidth used for the cacheline queue (decimal between 0 and 1) 
   bool m_page_inject_delay_when_queue_full;       // When page queue full, have new page requests wait until it's not full
   bool m_cacheline_inject_delay_when_queue_full;  // When cacheline queue full, have new cacheline requests wait until it's not full

   String m_name;                                  // Name of the queue, for debugging
   
   double m_specified_bw_GB_per_s;                                 // The specified bandwidth this queue model is supposed to have, in GB/s
   double m_max_bandwidth_allowable_excess_ratio;                  // Allow effective bandwidth to exceed the specified bandwidth by at most this ratio
   UInt64 m_effective_bandwidth_exceeded_allowable_max;            // The number of actual queue requests for which the effective bandwidth in the window exceeded m_specified_bw_GB_per_s * m_max_bandwidth_allowable_excess_ratio
   UInt64 m_page_effective_bandwidth_exceeded_allowable_max;       // The number of actual queue requests for which the effective page bandwidth in the window exceeded m_specified_bw_GB_per_s * (1 - m_r_cacheline_queue_fraction) * m_max_bandwidth_allowable_excess_ratio
   UInt64 m_cacheline_effective_bandwidth_exceeded_allowable_max;  // The number of actual queue requests for which the effective cacheline bandwidth in the window exceeded m_specified_bw_GB_per_s * m_r_cacheline_queue_fraction * m_max_bandwidth_allowable_excess_ratio

   double m_max_effective_bandwidth;                               // in bytes / ps
   // The following two variables are to register stats
   UInt64 m_max_effective_bandwidth_bytes;
   UInt64 m_max_effective_bandwidth_ps;
   // std::vector<double> m_effective_bandwidth_tracker;           // (try to) track the distribution of effective bandwidth

   UInt64 m_page_bytes_tracking;                                   // track the total number of bytes being transferred in the current window
   UInt64 m_cacheline_bytes_tracking;                              // track the total number of bytes being transferred in the current window
   double m_page_max_effective_bandwidth;                          // in bytes / ps
   double m_cacheline_max_effective_bandwidth;                     // in bytes / ps
   // The following four variables are to register stats
   UInt64 m_page_max_effective_bandwidth_bytes;
   UInt64 m_page_max_effective_bandwidth_ps;
   UInt64 m_cacheline_max_effective_bandwidth_bytes;
   UInt64 m_cacheline_max_effective_bandwidth_ps;
   std::multimap<SubsecondTime, UInt64> m_page_packet_bytes;       // track the number of bytes of each packet being transferred in the current window
   std::multimap<SubsecondTime, UInt64> m_cacheline_packet_bytes;  // track the number of bytes of each packet being transferred in the current window
   
   // For stats
   UInt64 m_total_requests;
   UInt64 m_total_page_requests;
   UInt64 m_total_cacheline_requests;
   UInt64 m_total_page_requests_queue_full;                        // The number of requests where queue utilization was full
   UInt64 m_total_page_requests_capped_by_window_size;             // Note: When m_use_separate_queue_delay_cap is true, this counts requests with calculated queue delay larger than window_size, but weren't actually capped
   UInt64 m_total_cacheline_requests_queue_full;                   // The number of requests where queue utilization was full
   UInt64 m_total_cacheline_requests_capped_by_window_size;
   // UInt64 m_total_requests_capped_by_queue_delay_cap;              // The number of requests where the returned queue delay was capped by the separate m_queue_delay_cap
   SubsecondTime m_total_utilized_time;
   SubsecondTime m_total_queue_delay;
   SubsecondTime m_total_page_queue_delay;
   SubsecondTime m_total_cacheline_queue_delay;

   SubsecondTime m_page_utilization_full_injected_delay;
   SubsecondTime m_cacheline_utilization_full_injected_delay;
   SubsecondTime m_page_utilization_full_injected_delay_max;
   SubsecondTime m_cacheline_utilization_full_injected_delay_max;
   
   double m_total_page_queue_utilization_during_cacheline_requests = 0.0;
   double m_total_cacheline_queue_utilization_during_page_requests = 0.0;
   UInt64 m_total_page_queue_utilization_during_cacheline_requests_numerator;
   UInt64 m_total_page_queue_utilization_during_cacheline_requests_denominator;  // Set as 10^6 in constructor function
   UInt64 m_total_cacheline_queue_utilization_during_page_requests_numerator;
   UInt64 m_total_cacheline_queue_utilization_during_page_requests_denominator;  // Set as 10^6 in constructor function

   double m_total_page_queue_utilization_during_cacheline_no_effect = 0.0;
   double m_total_cacheline_queue_utilization_during_page_no_effect = 0.0;
   UInt64 m_total_page_queue_utilization_during_cacheline_no_effect_numerator;
   UInt64 m_total_page_queue_utilization_during_cacheline_no_effect_denominator;  // Set as 10^6 in constructor function
   UInt64 m_total_cacheline_queue_utilization_during_page_no_effect_numerator;
   UInt64 m_total_cacheline_queue_utilization_during_page_no_effect_denominator;  // Set as 10^6 in constructor function

   UInt64 m_total_no_effect_page_requests = 0;
   UInt64 m_total_no_effect_cacheline_requests = 0;

   bool m_print_debugging_info = false;
   
   void addItem(SubsecondTime pkt_time, SubsecondTime service_time, request_t request_type);
   void removeItems(SubsecondTime earliest_time);

   void addItemUpdateBytes(SubsecondTime pkt_time, UInt64 num_bytes, SubsecondTime pkt_queue_delay, request_t request_type);
   void removeItemsUpdateBytes(SubsecondTime earliest_time, SubsecondTime pkt_time, bool track_effective_bandwidth, request_t request_type = QueueModel::PAGE);

   void updateQueues(SubsecondTime pkt_time, bool track_effective_bandwidth = false, request_t request_type = QueueModel::PAGE);

   SubsecondTime applyQueueDelayFormula(request_t request_type, UInt64 total_service_time, UInt64 total_service_time2, UInt64 num_arrivals, UInt64 utilization_window_size, bool update_stats, SubsecondTime pkt_time, SubsecondTime &utilization_overflow_wait_time);
   // Wrappers for two commonly used configurations
   SubsecondTime applySingleWindowSizeFormula(request_t request_type, UInt64 total_service_time, UInt64 total_service_time2, UInt64 num_arrivals, bool update_stats, SubsecondTime pkt_time, SubsecondTime &utilization_overflow_wait_time);
   // SubsecondTime applyDoubleWindowSizeFormula(request_t request_type, UInt64 total_service_time, UInt64 total_service_time2, UInt64 num_arrivals, bool update_stats, SubsecondTime pkt_time, SubsecondTime &utilization_overflow_wait_time);
};

#endif /* __QUEUE_MODEL_WINDOWED_MG1_REMOTE_IND_QUEUES_H__ */
