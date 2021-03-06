/** This file is based off of the modified queue_model_windowed_mg1_remote_combined. 
 * At the time of writing, the main differences are:
 *  1) Tries to account for both 
*/

#include "queue_model_windowed_mg1_remote_subqueuemodels.h"
#include "simulator.h"
#include "config.hpp"
#include "log.h"
#include "stats.h"

// #include <algorithm>

QueueModelWindowedMG1Subqueuemodels::QueueModelWindowedMG1Subqueuemodels(String name, UInt32 id, UInt64 bw_bits_per_us)
   : m_window_size(SubsecondTime::NS(Sim()->getCfg()->getInt("queue_model/windowed_mg1_remote_subqueuemodels/window_size")))
   , m_use_separate_queue_delay_cap(Sim()->getCfg()->getBool("queue_model/windowed_mg1_remote_subqueuemodels/use_separate_queue_delay_cap"))
   , m_use_utilization_overflow(Sim()->getCfg()->getBool("queue_model/windowed_mg1_remote_subqueuemodels/use_utilization_overflow"))
   , m_page_queue_overflowed_to_cacheline_queue(0)
   , m_cacheline_queue_overflowed_to_page_queue(0)
   , m_num_arrivals(0)
   , m_page_service_time_sum(0)
   , m_cacheline_service_time_sum(0)
   , m_page_service_time_sum2(0)
   , m_cacheline_service_time_sum2(0)
   , m_inflight_page_service_time_sum(0)
   , m_inflight_page_service_time_sum2(0)
   , m_cacheline_inserted_ahead_of_inflight_pages(0)
   , m_cacheline_inserted_ahead_of_inflight_pages_no_effect(0)
   , m_r_added_latency(SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_add_lat"))) // Network latency for remote DRAM access 
   , m_r_cacheline_queue_fraction(Sim()->getCfg()->getFloat("perf_model/dram/remote_cacheline_queue_fraction"))
   , m_name(name)
   , m_specified_bw_GB_per_s((double)bw_bits_per_us / 8 / 1000)  // convert bits/us to GB/s
   , m_max_bandwidth_allowable_excess_ratio(Sim()->getCfg()->getFloat("queue_model/windowed_mg1_remote_subqueuemodels/bandwidth_allowable_excess_ratio"))
   , m_effective_bandwidth_exceeded_allowable_max(0)
   , m_page_effective_bandwidth_exceeded_allowable_max(0)
   , m_cacheline_effective_bandwidth_exceeded_allowable_max(0)
   // , m_bytes_tracking(0)
   , m_max_effective_bandwidth(0.0)
   , m_max_effective_bandwidth_bytes(0)
   , m_max_effective_bandwidth_ps(1)  // dummy value; can't be 0
   , m_page_bytes_tracking(0)
   , m_cacheline_bytes_tracking(0)
   , m_page_max_effective_bandwidth(0.0)
   , m_cacheline_max_effective_bandwidth(0.0)
   , m_page_max_effective_bandwidth_bytes(0)
   , m_page_max_effective_bandwidth_ps(1)  // dummy value; can't be 0
   , m_cacheline_max_effective_bandwidth_bytes(0)
   , m_cacheline_max_effective_bandwidth_ps(1)  // dummy value; can't be 0
   , m_total_requests(0)
   , m_total_page_requests(0)
   , m_total_cacheline_requests(0)
   , m_total_page_requests_queue_full(0)
   , m_total_page_requests_capped_by_window_size(0)
   , m_total_cacheline_requests_queue_full(0)
   , m_total_cacheline_requests_capped_by_window_size(0)
   // , m_total_requests_capped_by_queue_delay_cap(0)
   , m_total_utilized_time(SubsecondTime::Zero())
   , m_total_queue_delay(SubsecondTime::Zero())
   , m_total_page_queue_delay(SubsecondTime::Zero())
   , m_total_cacheline_queue_delay(SubsecondTime::Zero())
   , m_total_cacheline_queue_delay_saved(SubsecondTime::Zero())
{  
   if (m_use_separate_queue_delay_cap) {
      m_queue_delay_cap = SubsecondTime::NS(Sim()->getCfg()->getInt("queue_model/windowed_mg1_remote_subqueuemodels/queue_delay_cap"));
   } else {
      m_queue_delay_cap = m_window_size;  // in this case m_queue_delay_cap won't be used, but set accordingly just in case
   }

   if (m_use_utilization_overflow) {
      m_utilization_overflow_threshold = Sim()->getCfg()->getFloat("queue_model/windowed_mg1_remote_subqueuemodels/subqueue_utilization_overflow_threshold");
   }
   
   registerStatsMetric(name, id, "num-requests", &m_total_requests);
   registerStatsMetric(name, id, "num-page-requests", &m_total_page_requests);
   registerStatsMetric(name, id, "num-cacheline-requests", &m_total_cacheline_requests);

   registerStatsMetric(name, id, "total-time-used", &m_total_utilized_time);
   registerStatsMetric(name, id, "total-queue-delay", &m_total_queue_delay);
   registerStatsMetric(name, id, "total-page-queue-delay", &m_total_page_queue_delay);
   registerStatsMetric(name, id, "total-cacheline-queue-delay", &m_total_cacheline_queue_delay);
   registerStatsMetric(name, id, "total-cacheline-queue-delay-saved", &m_total_cacheline_queue_delay_saved);
     
   registerStatsMetric(name, id, "num-page-requests-queue-full", &m_total_page_requests_queue_full);
   registerStatsMetric(name, id, "num-page-requests-capped-by-window-size", &m_total_page_requests_capped_by_window_size);
   registerStatsMetric(name, id, "num-cacheline-requests-queue-full", &m_total_cacheline_requests_queue_full);
   registerStatsMetric(name, id, "num-cacheline-requests-capped-by-window-size", &m_total_cacheline_requests_capped_by_window_size);
   // registerStatsMetric(name, id, "num-requests-capped-by-custom-cap", &m_total_requests_capped_by_queue_delay_cap);

   registerStatsMetric(name, id, "num-requests-page-queue-overflowed-to-cacheline-queue", &m_page_queue_overflowed_to_cacheline_queue);
   registerStatsMetric(name, id, "num-requests-cacheline-queue-overflowed-to-page-queue", &m_cacheline_queue_overflowed_to_page_queue);

   registerStatsMetric(name, id, "num-requests-cacheline-inserted-ahead-of-inflight-pages", &m_cacheline_inserted_ahead_of_inflight_pages);
   registerStatsMetric(name, id, "num-no-effect-requests-cacheline-inserted-ahead-of-inflight-pages", &m_cacheline_inserted_ahead_of_inflight_pages_no_effect);

   registerStatsMetric(name, id, "total-cacheline-queue-utilization-during-page-requests-numerator", &m_total_cacheline_queue_utilization_during_page_requests_numerator);
   registerStatsMetric(name, id, "total-cacheline-queue-utilization-during-page-requests-denominator", &m_total_cacheline_queue_utilization_during_page_requests_denominator);
   registerStatsMetric(name, id, "total-page-queue-utilization-during-cacheline-requests-numerator", &m_total_page_queue_utilization_during_cacheline_requests_numerator);
   registerStatsMetric(name, id, "total-page-queue-utilization-during-cacheline-requests-denominator", &m_total_page_queue_utilization_during_cacheline_requests_denominator);

   registerStatsMetric(name, id, "total-cacheline-queue-utilization-during-page-no-effect-numerator", &m_total_cacheline_queue_utilization_during_page_no_effect_numerator);
   registerStatsMetric(name, id, "total-cacheline-queue-utilization-during-page-no-effect-denominator", &m_total_cacheline_queue_utilization_during_page_no_effect_denominator);
   registerStatsMetric(name, id, "total-page-queue-utilization-during-cacheline-no-effect-numerator", &m_total_page_queue_utilization_during_cacheline_no_effect_numerator);
   registerStatsMetric(name, id, "total-page-queue-utilization-during-cacheline-no-effect-denominator", &m_total_page_queue_utilization_during_cacheline_no_effect_denominator);
   registerStatsMetric(name, id, "num-no-effect-page-requests", &m_total_no_effect_page_requests);
   registerStatsMetric(name, id, "num-no-effect-cacheline-requests", &m_total_no_effect_cacheline_requests);

   // Divide the first following stat by the second one to get bytes / ps (can't register a double type as a stat)
   // The only reason why it's in bytes/ps is because these were the raw values used to compute effective bandwidth
   // in this class. Considering converting to GB/s like other bandwidth values?
   registerStatsMetric(name, id, "max-effective-bandwidth-bytes", &m_max_effective_bandwidth_bytes);
   registerStatsMetric(name, id, "max-effective-bandwidth-ps", &m_max_effective_bandwidth_ps);
   registerStatsMetric(name, id, "page-max-effective-bandwidth-bytes", &m_page_max_effective_bandwidth_bytes);
   registerStatsMetric(name, id, "page-max-effective-bandwidth-ps", &m_page_max_effective_bandwidth_ps);
   registerStatsMetric(name, id, "cacheline-max-effective-bandwidth-bytes", &m_cacheline_max_effective_bandwidth_bytes);
   registerStatsMetric(name, id, "cacheline-max-effective-bandwidth-ps", &m_cacheline_max_effective_bandwidth_ps);

   // Only tracked m_total_requests is incremented and addItems() is called (ie once per actual queue delay request)
   registerStatsMetric(name, id, "num-effective-bandwidth-exceeded-allowable-max", &m_effective_bandwidth_exceeded_allowable_max);
   registerStatsMetric(name, id, "num-page-effective-bandwidth-exceeded-allowable-max", &m_page_effective_bandwidth_exceeded_allowable_max);
   registerStatsMetric(name, id, "num-cacheline-effective-bandwidth-exceeded-allowable-max", &m_cacheline_effective_bandwidth_exceeded_allowable_max);

   std::cout << "Using windowed_mg1_remote_subqueuemodels queue model with m_use_utilization_overflow=" << m_use_utilization_overflow;
   if (m_use_utilization_overflow) {
      std::cout << ", utilization overflow threshold=" << m_utilization_overflow_threshold;
   }
   std::cout << std::endl;
}

QueueModelWindowedMG1Subqueuemodels::~QueueModelWindowedMG1Subqueuemodels()
{
   if (m_total_requests < 1) {
      return;
   }
   // Print one time debug output in the destructor method, to be printed once when the program finishes
   // if (m_effective_bandwidth_tracker.size() < 1) {
   //    return;
   // }
   std::cout << "Queue " << m_name << ":" << std::endl; 
   std::cout << "m_max_effective_bandwidth gave: " << 1000 * m_max_effective_bandwidth << " GB/s" << std::endl;

   // // Compute approximate percentiles of m_effective_bandwidth_tracker
   // std::sort(m_effective_bandwidth_tracker.begin(), m_effective_bandwidth_tracker.end());

   // // Compute percentiles for stats
   // UInt64 index;
   // double percentile;
   
   // // Compute more percentiles for output
   // UInt32 num_bins = 40;  // the total number of points is 1 more than num_bins, since it includes the endpoints
   // std::map<double, double> effective_bandwidth_percentiles;
   // for (UInt32 bin = 0; bin < num_bins; ++bin) {
   //    double percentage = (double)bin / num_bins;
   //    index = (UInt64)(percentage * (m_effective_bandwidth_tracker.size() - 1));  // -1 so array indices don't go out of bounds
   //    percentile = m_effective_bandwidth_tracker[index];  // in bytes/ps
   //    std::cout << "percentage: " << percentage << ", vector index: " << index << ", percentile: " << 1000 * percentile << " GB/s" << std::endl;
   //    effective_bandwidth_percentiles.insert(std::pair<double, double>(percentage, percentile));
   // }
   // // Print output in format that can easily be graphed in Python
   // std::ostringstream percentages_buffer;
   // std::ostringstream cdf_buffer;
   // percentages_buffer << "[";
   // cdf_buffer << "[";
   // for (std::map<double, double>::iterator it = effective_bandwidth_percentiles.begin(); it != effective_bandwidth_percentiles.end(); ++it) {
   //    percentages_buffer << it->first << ", ";
   //    cdf_buffer << 1000 * it->second << ", ";  // Convert to GB/s
   // }
   // percentages_buffer << "]";
   // cdf_buffer << "]";

   // std::cout << "CDF X values (bandwidth), in GB/s:\n" << cdf_buffer.str() << std::endl;
   // std::cout << "CDF Y values (probability):\n" << percentages_buffer.str() << std::endl;

   if ((double) m_effective_bandwidth_exceeded_allowable_max / m_total_requests > 0.01) {
      std::cout << "Queue " << m_name << " had " << 100 * (double)m_effective_bandwidth_exceeded_allowable_max / m_total_requests;
      std::cout << "% of windows with effective bandwidth that exceeded the allowable max bandwidth of " << m_specified_bw_GB_per_s * m_max_bandwidth_allowable_excess_ratio << "GB/s" << std::endl;
   }

   if (m_total_page_requests > 0) {
      if ((double) m_page_effective_bandwidth_exceeded_allowable_max / m_total_page_requests > 0.01) {
         std::cout << "Queue " << m_name << " page portion had " << 100 * (double)m_page_effective_bandwidth_exceeded_allowable_max / m_total_page_requests;
         std::cout << "% of windows with effective bandwidth that exceeded the allowable max bandwidth of " << m_specified_bw_GB_per_s * (1 - m_r_cacheline_queue_fraction) * m_max_bandwidth_allowable_excess_ratio << "GB/s" << std::endl;
      }
      std::cout << "Avg cacheline queue utilization during page requests: " << 100 * m_total_cacheline_queue_utilization_during_page_requests / m_total_page_requests << "%" << std::endl;
   }

   if (m_total_cacheline_requests > 0) {
      if ((double) m_cacheline_effective_bandwidth_exceeded_allowable_max / m_total_cacheline_requests > 0.01) {
         std::cout << "Queue " << m_name << " cacheline portion had " << 100 * (double)m_cacheline_effective_bandwidth_exceeded_allowable_max / m_total_cacheline_requests;
         std::cout << "% of windows with effective bandwidth that exceeded the allowable max bandwidth of " << m_specified_bw_GB_per_s * m_r_cacheline_queue_fraction * m_max_bandwidth_allowable_excess_ratio << "GB/s" << std::endl;
      }
      std::cout << "Avg page queue utilization during cacheline requests: " << 100 * m_total_page_queue_utilization_during_cacheline_requests / m_total_cacheline_requests << "%" << std::endl;
   }

   if (m_total_no_effect_page_requests > 0) {
      std::cout << "Avg cacheline queue utilization during page No Effect requests: " << 100 * m_total_cacheline_queue_utilization_during_page_no_effect / m_total_no_effect_page_requests << "%" << std::endl;
   }
   if (m_total_no_effect_cacheline_requests > 0) {
      std::cout << "Avg page queue utilization during cacheline No Effect requests: " << 100 * m_total_page_queue_utilization_during_cacheline_no_effect / m_total_no_effect_cacheline_requests << "%" << std::endl;
   }

   if (m_cacheline_inserted_queue_delay_larger_error)
      std::cout << "computeQueueDelayTrackBytesPotentialPushback() cacheline inserted ahead queue delay larger " << m_cacheline_inserted_queue_delay_larger_error << " times, something wrong?" << std::endl;
   if (m_delayed_queue_delay_smaller_error)
      std::cout << "computeQueueDelayTrackBytesPotentialPushback() inflight page delayed queue delay smaller " << m_delayed_queue_delay_smaller_error << " times, something wrong?" << std::endl;
}

void QueueModelWindowedMG1Subqueuemodels::finalizeStats() {
   // Try editing the value to trigger the variable to be recorded??
   m_total_cacheline_queue_utilization_during_page_requests_denominator = 1;
   m_total_page_queue_utilization_during_cacheline_requests_denominator = 1;
   m_total_cacheline_queue_utilization_during_page_no_effect_denominator = 1;
   m_total_page_queue_utilization_during_cacheline_no_effect_denominator = 1;

   m_total_cacheline_queue_utilization_during_page_requests_denominator = 1000 * 1000;
   m_total_page_queue_utilization_during_cacheline_requests_denominator = 1000 * 1000;
   m_total_cacheline_queue_utilization_during_page_no_effect_denominator = 1000 * 1000;
   m_total_page_queue_utilization_during_cacheline_no_effect_denominator = 1000 * 1000;
   std::cout << "remote_subqueuemodels finalizeStats(): denominator=" << m_total_cacheline_queue_utilization_during_page_requests_denominator << std::endl;
}

double QueueModelWindowedMG1Subqueuemodels::getPageQueueUtilizationPercentage(SubsecondTime pkt_time) {
   // Remove packets that now fall outside the window
   // Advance the window based on the global (barrier) time, as this guarantees the earliest time any thread may be at.
   // Use a backup value of 10 window sizes before the current request to avoid excessive memory usage in case something fishy is going on.
   SubsecondTime backup = pkt_time > 10*m_window_size ? pkt_time - 10*m_window_size : SubsecondTime::Zero();
   SubsecondTime main = Sim()->getClockSkewMinimizationServer()->getGlobalTime() > m_window_size ? Sim()->getClockSkewMinimizationServer()->getGlobalTime() - m_window_size : SubsecondTime::Zero();
   SubsecondTime time_point = SubsecondTime::max(main, backup);
   removeItems(time_point);
   removeItemsUpdateBytes(time_point, pkt_time, false);

   // Use queue utilization as measure to determine whether the queue is full
   double utilization = (double)m_page_service_time_sum / m_window_size.getPS();
   return utilization;
}

double QueueModelWindowedMG1Subqueuemodels::getCachelineQueueUtilizationPercentage(SubsecondTime pkt_time) {
   // Remove packets that now fall outside the window
   // Advance the window based on the global (barrier) time, as this guarantees the earliest time any thread may be at.
   // Use a backup value of 10 window sizes before the current request to avoid excessive memory usage in case something fishy is going on.
   SubsecondTime backup = pkt_time > 10*m_window_size ? pkt_time - 10*m_window_size : SubsecondTime::Zero();
   SubsecondTime main = Sim()->getClockSkewMinimizationServer()->getGlobalTime() > m_window_size ? Sim()->getClockSkewMinimizationServer()->getGlobalTime() - m_window_size : SubsecondTime::Zero();
   SubsecondTime time_point = SubsecondTime::max(main, backup);
   removeItems(time_point);
   removeItemsUpdateBytes(time_point, pkt_time, false);

   // Use queue utilization as measure to determine whether the queue is full
   double utilization = (double)m_cacheline_service_time_sum / m_window_size.getPS();
   return utilization;
}

double QueueModelWindowedMG1Subqueuemodels::getTotalQueueUtilizationPercentage(SubsecondTime pkt_time) {
   // Remove packets that now fall outside the window
   // Advance the window based on the global (barrier) time, as this guarantees the earliest time any thread may be at.
   // Use a backup value of 10 window sizes before the current request to avoid excessive memory usage in case something fishy is going on.
   SubsecondTime backup = pkt_time > 10*m_window_size ? pkt_time - 10*m_window_size : SubsecondTime::Zero();
   SubsecondTime main = Sim()->getClockSkewMinimizationServer()->getGlobalTime() > m_window_size ? Sim()->getClockSkewMinimizationServer()->getGlobalTime() - m_window_size : SubsecondTime::Zero();
   SubsecondTime time_point = SubsecondTime::max(main, backup);
   removeItems(time_point);
   removeItemsUpdateBytes(time_point, pkt_time, false);

   // Use queue utilization as measure to determine whether the queue is full
   double utilization = (double)(m_page_service_time_sum + m_cacheline_service_time_sum) / (2 * m_window_size.getPS());
   return utilization;
}

// With computeQueueDelayTrackBytes(), computeQueueDelay() shouldn't be used anymore
SubsecondTime
QueueModelWindowedMG1Subqueuemodels::computeQueueDelay(SubsecondTime pkt_time, SubsecondTime processing_time, core_id_t requester)
{
   throw std::logic_error("QueueModelWindowedMG1Subqueuemodels::computeQueueDelay shouldn't be called");
}

/* Get estimate of queue delay without adding the packet to the queue */
SubsecondTime
QueueModelWindowedMG1Subqueuemodels::computeQueueDelayNoEffect(SubsecondTime pkt_time, SubsecondTime processing_time, request_t request_type, core_id_t requester)
{
   SubsecondTime t_queue = SubsecondTime::Zero();

   // Advance the window based on the global (barrier) time, as this guarantees the earliest time any thread may be at.
   // Use a backup value of 10 window sizes before the current request to avoid excessive memory usage in case something fishy is going on.
   SubsecondTime backup = pkt_time > 10*m_window_size ? pkt_time - 10*m_window_size : SubsecondTime::Zero();
   SubsecondTime main = Sim()->getClockSkewMinimizationServer()->getGlobalTime() > m_window_size ? Sim()->getClockSkewMinimizationServer()->getGlobalTime() - m_window_size : SubsecondTime::Zero();
   SubsecondTime time_point = SubsecondTime::max(main, backup);
   removeItems(time_point);
   removeItemsUpdateBytes(time_point, pkt_time, false);

   double page_queue_utilization_percentage = getPageQueueUtilizationPercentage(pkt_time);
   double cacheline_queue_utilization_percentage = getCachelineQueueUtilizationPercentage(pkt_time);

   if (m_num_arrivals > 1)
   {
      UInt64 service_time_sum;
      UInt64 service_time_sum2;
      
      // // Process differently whether this is a page or cacheline access
      // if (request_type == QueueModel::PAGE) {
      //    service_time_sum = m_page_service_time_sum;
      //    service_time_sum2 = m_page_service_time_sum2;
      // } else {  // request_type == QueueModel::CACHELINE
      //    service_time_sum = m_cacheline_service_time_sum;
      //    service_time_sum2 = m_cacheline_service_time_sum2;
      // }
      // Include the incoming packet in calculations to differentiate between small and large requests in decision making.
      // Don't need to update m_num_arrivals since it gets cancelled out in the formula
      service_time_sum = m_page_service_time_sum + m_cacheline_service_time_sum + processing_time.getPS();
      service_time_sum2 = m_page_service_time_sum2 + m_cacheline_service_time_sum2 + processing_time.getPS() * processing_time.getPS();
      if (request_type == QueueModel::CACHELINE && m_inflight_page_service_time_sum > 0) {
         // As if cacheline request was made before the inflight pages
         service_time_sum = m_page_service_time_sum + m_cacheline_service_time_sum - m_inflight_page_service_time_sum + processing_time.getPS();
         service_time_sum2 = m_page_service_time_sum2 + m_cacheline_service_time_sum2 - m_inflight_page_service_time_sum2 + processing_time.getPS() * processing_time.getPS();
         
         ++m_cacheline_inserted_ahead_of_inflight_pages_no_effect;       
      }
      t_queue = applyDoubleWindowSizeFormula(request_type, service_time_sum, service_time_sum2, m_num_arrivals, false);
   }
   // Add additional network latency
   t_queue += m_r_added_latency;  // is it ok for t_queue to potentially be larger than m_window_size?

   // removed addItem() call and calls that updated stats
   
   // Stats for diagnosing
   if (request_type == QueueModel::PAGE) {
      ++m_total_no_effect_page_requests;
      m_total_cacheline_queue_utilization_during_page_no_effect += cacheline_queue_utilization_percentage;
      m_total_cacheline_queue_utilization_during_page_no_effect_numerator = (UInt64)(m_total_cacheline_queue_utilization_during_page_no_effect * m_total_cacheline_queue_utilization_during_page_no_effect_denominator);
   } else {  // request_type == QueueModel::CACHELINE
      ++m_total_no_effect_cacheline_requests;
      m_total_page_queue_utilization_during_cacheline_no_effect += page_queue_utilization_percentage;
      m_total_page_queue_utilization_during_cacheline_no_effect_numerator = (UInt64)(m_total_page_queue_utilization_during_cacheline_no_effect * m_total_page_queue_utilization_during_cacheline_no_effect_denominator);
   }

   return t_queue;
}

void
QueueModelWindowedMG1Subqueuemodels::removeInflightPage(UInt64 phys_page)
{
   if (m_inflight_page_tracking.count(phys_page)) {  // if phys_page is within the map
      SubsecondTime processing_time = m_inflight_page_tracking[phys_page].processing_time;
      // Update variables
      m_inflight_page_service_time_sum -= processing_time.getPS();
      m_inflight_page_service_time_sum2 -= processing_time.getPS() * processing_time.getPS();
      m_inflight_page_tracking.erase(phys_page);
   }
}

SubsecondTime
QueueModelWindowedMG1Subqueuemodels::computeQueueDelayTrackBytes(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, core_id_t requester, bool is_inflight_page, UInt64 phys_page)
{
   std::vector<std::pair<UInt64, SubsecondTime>> dummy_vector;  // ignore output that's put into this vector
   SubsecondTime queue_delay = computeQueueDelayTrackBytesPotentialPushback(pkt_time, processing_time, num_bytes, request_type, dummy_vector, false, requester);
   if (is_inflight_page) {
      struct InflightPageData inflight_page_data = {processing_time,
                                                    // the two variables below include the processing time of the current page, since this is after the addItem call in computeQueueDelayTrackBytes()
                                                    m_page_service_time_sum + m_cacheline_service_time_sum,
                                                    m_page_service_time_sum2 + m_cacheline_service_time_sum2,
                                                    queue_delay};
      m_inflight_page_tracking.insert(std::pair<UInt64, InflightPageData>(phys_page, inflight_page_data));
      // Update variables
      m_inflight_page_service_time_sum += processing_time.getPS();
      m_inflight_page_service_time_sum2 += processing_time.getPS() * processing_time.getPS();
   }
   return queue_delay;
}

// total_service_time is in ps, total_service_time2 is in ps^2, utilization_window_size is in ps
SubsecondTime
QueueModelWindowedMG1Subqueuemodels::applyQueueDelayFormula(request_t request_type, UInt64 total_service_time, UInt64 total_service_time2, UInt64 num_arrivals, UInt64 utilization_window_size, bool update_stats)
{
   double utilization = (double)total_service_time / utilization_window_size;
   double service_time_Es2 = total_service_time2 / num_arrivals;
   double arrival_rate = (double)num_arrivals / utilization_window_size;

   // If requesters do not throttle based on returned latency, it's their problem, not ours
   if (utilization > .9999) { // number here changed from .99 to .9999; still needed?
      utilization = .9999;
      if (update_stats && request_type == QueueModel::PAGE)
         ++m_total_page_requests_queue_full;
      else if (update_stats)  // request_type == QueueModel::CACHELINE
         ++m_total_cacheline_requests_queue_full;
   }

   SubsecondTime t_queue = SubsecondTime::PS(arrival_rate * service_time_Es2 / (2 * (1. - utilization)));
   // if (m_name == "dram-datamovement-queue")
   //    LOG_PRINT("t_queue initially calculated as %ld ns: %f / %f = %f ps", t_queue.getNS(),
   //              (arrival_rate * service_time_Es2), (2 * (1. - adjusted_utilization)), arrival_rate * service_time_Es2 / (2 * (1. - adjusted_utilization)));

   // Our memory is limited in time to m_window_size. It would be strange to return more latency than that.
   if (!m_use_separate_queue_delay_cap && t_queue > m_window_size) {
      // Normally, use m_window_size as the cap
      t_queue = m_window_size;
      if (update_stats && request_type == QueueModel::PAGE)
         ++m_total_page_requests_capped_by_window_size;
      else if (update_stats)  // request_type == QueueModel::CACHELINE
         ++m_total_cacheline_requests_capped_by_window_size;
   } else if (m_use_separate_queue_delay_cap) {
      if (t_queue > m_queue_delay_cap) {
         // When m_use_separate_queue_delay_cap is true, try using a custom queue_delay_cap to handle the cases when the queue is full
         t_queue = m_queue_delay_cap;
         // ++m_total_requests_capped_by_queue_delay_cap;
      }
      if (update_stats && t_queue > m_window_size) {
         // still keep track of this stat when separate queue delay cap is used
         if (request_type == QueueModel::PAGE)
            ++m_total_page_requests_capped_by_window_size;
         else  // request_type == QueueModel::CACHELINE
            ++m_total_cacheline_requests_capped_by_window_size;
      }
   }
   // Note: network additional latency is NOT added here, needs to be added separately
   return t_queue;
}

// total_service_time is in ps, total_service_time2 is in ps^2
SubsecondTime
QueueModelWindowedMG1Subqueuemodels::applySingleWindowSizeFormula(request_t request_type, UInt64 total_service_time, UInt64 total_service_time2, UInt64 num_arrivals, bool update_stats)
{
   return applyQueueDelayFormula(request_type, total_service_time, total_service_time2, num_arrivals, m_window_size.getPS(), update_stats);
}

// total_service_time is in ps, total_service_time2 is in ps^2
SubsecondTime
QueueModelWindowedMG1Subqueuemodels::applyDoubleWindowSizeFormula(request_t request_type, UInt64 total_service_time, UInt64 total_service_time2, UInt64 num_arrivals, bool update_stats)
{
   return applyQueueDelayFormula(request_type, total_service_time, total_service_time2, num_arrivals, 2 * m_window_size.getPS(), update_stats);
}

double
QueueModelWindowedMG1Subqueuemodels::squared(double x)
{
   return x * x;
}


// Right now new_inflight_page_arrival_time_deltas only supports positive deltas (ie pushing back the arrival time)
SubsecondTime
QueueModelWindowedMG1Subqueuemodels::computeQueueDelayTrackBytesPotentialPushback(SubsecondTime pkt_time, SubsecondTime processing_time, UInt64 num_bytes, request_t request_type, std::vector<std::pair<UInt64, SubsecondTime>>& new_inflight_page_arrival_time_deltas, bool compute_inflight_page_delays, core_id_t requester)
{
   SubsecondTime t_queue = SubsecondTime::Zero();

   // Advance the window based on the global (barrier) time, as this guarantees the earliest time any thread may be at.
   // Use a backup value of 10 window sizes before the current request to avoid excessive memory usage in case something fishy is going on.
   SubsecondTime backup = pkt_time > 10*m_window_size ? pkt_time - 10*m_window_size : SubsecondTime::Zero();
   SubsecondTime main = Sim()->getClockSkewMinimizationServer()->getGlobalTime() > m_window_size ? Sim()->getClockSkewMinimizationServer()->getGlobalTime() - m_window_size : SubsecondTime::Zero();
   SubsecondTime time_point = SubsecondTime::max(main, backup);
   removeItems(time_point);
   removeItemsUpdateBytes(time_point, pkt_time, true);  // only track effective bandwidth when m_total_requests is incremented and addItems() is called

   double page_queue_utilization_percentage = getPageQueueUtilizationPercentage(pkt_time);
   double cacheline_queue_utilization_percentage = getCachelineQueueUtilizationPercentage(pkt_time);

   if (m_num_arrivals > 1)  // Share num_arrivals among both queues, since the number gets cancelled out anyways
   {
      UInt64 service_time_sum;
      UInt64 service_time_sum2;
      
      // // Process differently whether this is a page or cacheline access
      // if (request_type == QueueModel::PAGE) {
      //    service_time_sum = m_page_service_time_sum;
      //    service_time_sum2 = m_page_service_time_sum2;
      // } else {  // request_type == QueueModel::CACHELINE
      //    service_time_sum = m_cacheline_service_time_sum;
      //    service_time_sum2 = m_cacheline_service_time_sum2;
      // }
      // Include the incoming packet in calculations to differentiate between small and large requests in decision making.
      // Don't need to update m_num_arrivals since it gets cancelled out in the formula
      service_time_sum = m_page_service_time_sum + m_cacheline_service_time_sum + processing_time.getPS();
      service_time_sum2 = m_page_service_time_sum2 + m_cacheline_service_time_sum2 + processing_time.getPS() * processing_time.getPS();
      SubsecondTime old_queue_delay = applyDoubleWindowSizeFormula(request_type, service_time_sum, service_time_sum2, m_num_arrivals, false);
      if (request_type == QueueModel::CACHELINE && compute_inflight_page_delays && m_inflight_page_service_time_sum > 0) {
         // As if cacheline request was made before the inflight pages
         service_time_sum = m_page_service_time_sum + m_cacheline_service_time_sum - m_inflight_page_service_time_sum + processing_time.getPS();
         service_time_sum2 = m_page_service_time_sum2 + m_cacheline_service_time_sum2 - m_inflight_page_service_time_sum2 + processing_time.getPS() * processing_time.getPS();

         ++m_cacheline_inserted_ahead_of_inflight_pages;
         // Calculate inflight page arrival times based on queue delay that would be returned if the cacheline request was previously made
         for (auto it = m_inflight_page_tracking.begin(); it != m_inflight_page_tracking.end(); ++it) {
            UInt64 inflight_page_service_time_sum = it->second.service_time_sum + processing_time.getPS();
            UInt64 inflight_page_service_time_sum2 = it->second.service_time_sum2 + processing_time.getPS() * processing_time.getPS();

            SubsecondTime new_queue_delay = applyDoubleWindowSizeFormula(QueueModel::PAGE, inflight_page_service_time_sum, inflight_page_service_time_sum2, m_num_arrivals, false);
            // Add additional network latency
            new_queue_delay += m_r_added_latency;  // is it ok for new_queue_delay to potentially be larger than m_window_size?

            if (new_queue_delay < it->second.prev_queue_delay) {
               ++m_delayed_queue_delay_smaller_error;
            } else {
               new_inflight_page_arrival_time_deltas.push_back(std::pair<UInt64, SubsecondTime>(it->first, new_queue_delay - it->second.prev_queue_delay));
               it->second.prev_queue_delay = new_queue_delay;
            }
         }
      }
      t_queue = applyDoubleWindowSizeFormula(request_type, service_time_sum, service_time_sum2, m_num_arrivals, true);
      if (request_type == QueueModel::CACHELINE && compute_inflight_page_delays && m_inflight_page_service_time_sum > 0) {
         if (t_queue > old_queue_delay) {
            ++m_cacheline_inserted_queue_delay_larger_error;
         } else {
            m_total_cacheline_queue_delay_saved += old_queue_delay - t_queue;
         }
      }
   }
   // Add additional network latency
   t_queue += m_r_added_latency;  // is it ok for t_queue to potentially be larger than m_window_size?

   // If one queue's utilization is high, add it to the other queue if the other queue has space
   if (m_use_utilization_overflow && request_type == QueueModel::PAGE && page_queue_utilization_percentage > m_utilization_overflow_threshold && cacheline_queue_utilization_percentage < m_utilization_overflow_threshold - 0.1) {
      // Add to cacheline queue instead (fills its utilization)
      addItem(pkt_time, processing_time, QueueModel::CACHELINE);
      addItemUpdateBytes(pkt_time, num_bytes, t_queue, QueueModel::CACHELINE);
      ++m_page_queue_overflowed_to_cacheline_queue;
   } else if (m_use_utilization_overflow && request_type == QueueModel::CACHELINE && cacheline_queue_utilization_percentage > m_utilization_overflow_threshold && page_queue_utilization_percentage < m_utilization_overflow_threshold - 0.1) {
      // Add to page queue instead (fills its utilization)
      addItem(pkt_time, processing_time, QueueModel::PAGE);
      addItemUpdateBytes(pkt_time, num_bytes, t_queue, QueueModel::PAGE);
      ++m_cacheline_queue_overflowed_to_page_queue;
   } else {
      addItem(pkt_time, processing_time, request_type);
      addItemUpdateBytes(pkt_time, num_bytes, t_queue, request_type);
   }


   m_total_utilized_time += processing_time;
   ++m_total_requests;
   m_total_queue_delay += t_queue;
   if (request_type == QueueModel::PAGE) {
      ++m_total_page_requests;
      m_total_page_queue_delay += t_queue;
      m_total_cacheline_queue_utilization_during_page_requests += cacheline_queue_utilization_percentage;
      m_total_cacheline_queue_utilization_during_page_requests_numerator = (UInt64)(m_total_cacheline_queue_utilization_during_page_requests * m_total_cacheline_queue_utilization_during_page_requests_denominator);
   } else {  // request_type == QueueModel::CACHELINE
      ++m_total_cacheline_requests;
      m_total_cacheline_queue_delay += t_queue;
      m_total_page_queue_utilization_during_cacheline_requests += page_queue_utilization_percentage;
      m_total_page_queue_utilization_during_cacheline_requests_numerator = (UInt64)(m_total_page_queue_utilization_during_cacheline_requests * m_total_page_queue_utilization_during_cacheline_requests_denominator);
   }

   return t_queue;
}

void
QueueModelWindowedMG1Subqueuemodels::addItemUpdateBytes(SubsecondTime pkt_time, UInt64 num_bytes, SubsecondTime pkt_queue_delay, request_t request_type)
{
   // Add num_bytes to map tracking the current window & update m_bytes_tracking
   // m_packet_bytes.insert(std::pair<SubsecondTime, UInt64>(pkt_time + pkt_queue_delay, num_bytes));
   // m_bytes_tracking += num_bytes;
   if (request_type == QueueModel::PAGE) {
      m_page_packet_bytes.insert(std::pair<SubsecondTime, UInt64>(pkt_time + pkt_queue_delay, num_bytes));
      m_page_bytes_tracking += num_bytes;
   } else {  // request_type == QueueModel::CACHELINE
      m_cacheline_packet_bytes.insert(std::pair<SubsecondTime, UInt64>(pkt_time + pkt_queue_delay, num_bytes));
      m_cacheline_bytes_tracking += num_bytes;
   }
}

void
QueueModelWindowedMG1Subqueuemodels::removeItemsUpdateBytes(SubsecondTime earliest_time, SubsecondTime pkt_time, bool track_effective_bandwidth)
{
   // Can remove packets that arrived earlier than the current window
   // while(!m_packet_bytes.empty() && m_packet_bytes.begin()->first < earliest_time)
   // {
   //    std::multimap<SubsecondTime, UInt64>::iterator bytes_entry = m_packet_bytes.begin();
   //    m_bytes_tracking -= bytes_entry->second;
   //    m_packet_bytes.erase(bytes_entry);
   // }
   while(!m_page_packet_bytes.empty() && m_page_packet_bytes.begin()->first < earliest_time)
   {
      std::multimap<SubsecondTime, UInt64>::iterator bytes_entry = m_page_packet_bytes.begin();
      m_page_bytes_tracking -= bytes_entry->second;
      m_page_packet_bytes.erase(bytes_entry);
   }
   while(!m_cacheline_packet_bytes.empty() && m_cacheline_packet_bytes.begin()->first < earliest_time)
   {
      std::multimap<SubsecondTime, UInt64>::iterator bytes_entry = m_cacheline_packet_bytes.begin();
      m_cacheline_bytes_tracking -= bytes_entry->second;
      m_cacheline_packet_bytes.erase(bytes_entry);
   }
   
   // Update bytes_in_window by not considering packets in m_cacheline_packet_bytes and m_page_packet_bytes that arrive later than pkt_time
   // UInt64 bytes_in_window = m_bytes_tracking;
   // for (std::multimap<SubsecondTime, UInt64>::iterator bytes_entry = m_packet_bytes.upper_bound(pkt_time); bytes_entry != m_packet_bytes.end(); ++bytes_entry) {
   //    bytes_in_window -= bytes_entry->second;
   // }
   UInt64 page_bytes_in_window = m_page_bytes_tracking;
   UInt64 cacheline_bytes_in_window = m_cacheline_bytes_tracking;
   for (std::multimap<SubsecondTime, UInt64>::iterator bytes_entry = m_page_packet_bytes.upper_bound(pkt_time); bytes_entry != m_page_packet_bytes.end(); ++bytes_entry) {
      page_bytes_in_window -= bytes_entry->second;
   }
   for (std::multimap<SubsecondTime, UInt64>::iterator bytes_entry = m_cacheline_packet_bytes.upper_bound(pkt_time); bytes_entry != m_cacheline_packet_bytes.end(); ++bytes_entry) {
      cacheline_bytes_in_window -= bytes_entry->second;
   }
   UInt64 bytes_in_window = page_bytes_in_window + cacheline_bytes_in_window;

   
   // Intention: only track effective bandwidth when m_total_requests is incremented and addItems() is called
   if (track_effective_bandwidth) {
      // Compute the effective current window length, and calculate the current effective bandwidth
      // Note: bytes_in_window is used instead of m_bytes_tracking
      UInt64 effective_window_length_ps = (pkt_time - earliest_time).getPS();
      double cur_effective_bandwidth = (double)bytes_in_window / effective_window_length_ps;  // in bytes/ps
      if (cur_effective_bandwidth > m_max_effective_bandwidth) {
         m_max_effective_bandwidth = cur_effective_bandwidth;
         m_max_effective_bandwidth_bytes = bytes_in_window;
         m_max_effective_bandwidth_ps = effective_window_length_ps;
      }
      if (cur_effective_bandwidth * 1000 > m_specified_bw_GB_per_s * m_max_bandwidth_allowable_excess_ratio) {  // GB/s used here
         ++m_effective_bandwidth_exceeded_allowable_max;
      }
      // Calculate max effective bandwidth of page and cacheline portions--though they could exceed their specifications at times
      double cur_page_effective_bandwidth = (double)page_bytes_in_window / effective_window_length_ps;  // in bytes/ps
      if (cur_page_effective_bandwidth > m_page_max_effective_bandwidth) {
         m_page_max_effective_bandwidth = cur_page_effective_bandwidth;
         m_page_max_effective_bandwidth_bytes = page_bytes_in_window;
         m_page_max_effective_bandwidth_ps = effective_window_length_ps;
      }
      if (cur_page_effective_bandwidth * 1000 > m_specified_bw_GB_per_s * (1 - m_r_cacheline_queue_fraction) * m_max_bandwidth_allowable_excess_ratio) {  // GB/s used here
         ++m_page_effective_bandwidth_exceeded_allowable_max;
      }
      double cur_cacheline_effective_bandwidth = (double)cacheline_bytes_in_window / effective_window_length_ps;  // in bytes/ps
      if (cur_cacheline_effective_bandwidth > m_cacheline_max_effective_bandwidth) {
         m_cacheline_max_effective_bandwidth = cur_cacheline_effective_bandwidth;
         m_cacheline_max_effective_bandwidth_bytes = cacheline_bytes_in_window;
         m_cacheline_max_effective_bandwidth_ps = effective_window_length_ps;
      }
      if (cur_cacheline_effective_bandwidth * 1000 > m_specified_bw_GB_per_s * m_r_cacheline_queue_fraction * m_max_bandwidth_allowable_excess_ratio) {  // GB/s used here
         ++m_cacheline_effective_bandwidth_exceeded_allowable_max;
      }

      // m_effective_bandwidth_tracker.push_back(cur_effective_bandwidth);
   }
}


void
QueueModelWindowedMG1Subqueuemodels::addItem(SubsecondTime pkt_time, SubsecondTime service_time, request_t request_type)
{
   if (request_type == QueueModel::PAGE) {
      m_window_page_requests.insert(std::pair<SubsecondTime, SubsecondTime>(pkt_time, service_time));
      m_page_service_time_sum += service_time.getPS();
      m_page_service_time_sum2 += service_time.getPS() * service_time.getPS();
   } else {  // request_type == QueueModel::CACHELINE
      m_window_cacheline_requests.insert(std::pair<SubsecondTime, SubsecondTime>(pkt_time, service_time));
      m_cacheline_service_time_sum += service_time.getPS();
      m_cacheline_service_time_sum2 += service_time.getPS() * service_time.getPS();
   }
   m_num_arrivals ++;
}

void
QueueModelWindowedMG1Subqueuemodels::removeItems(SubsecondTime earliest_time)
{
   // Page request map
   while(!m_window_page_requests.empty() && m_window_page_requests.begin()->first < earliest_time)
   {
      std::multimap<SubsecondTime, SubsecondTime>::iterator entry = m_window_page_requests.begin();
      m_num_arrivals --;
      m_page_service_time_sum -= entry->second.getPS();
      m_page_service_time_sum2 -= entry->second.getPS() * entry->second.getPS();
      m_window_page_requests.erase(entry);
   }
   // Cacheline request map
   while(!m_window_cacheline_requests.empty() && m_window_cacheline_requests.begin()->first < earliest_time)
   {
      std::multimap<SubsecondTime, SubsecondTime>::iterator entry = m_window_cacheline_requests.begin();
      m_num_arrivals --;
      m_cacheline_service_time_sum -= entry->second.getPS();
      // m_service_time_sum -= entry->second.getPS();  // TODO: remove
      m_cacheline_service_time_sum2 -= entry->second.getPS() * entry->second.getPS();
      m_window_cacheline_requests.erase(entry);
   }
}
