#include "dram_perf_model_disagg_remote_only.h"
#include "simulator.h"
#include "config.h"
#include "config.hpp"
#include "stats.h"
#include "shmem_perf.h"
#include "address_home_lookup.h"
#include "utils.h"

#include <algorithm>

DramPerfModelDisaggRemoteOnly::DramPerfModelDisaggRemoteOnly(core_id_t core_id, UInt32 cache_block_size, AddressHomeLookup* address_home_lookup)
    : DramPerfModel(core_id, cache_block_size)
    , m_core_id(core_id)
    , m_address_home_lookup(address_home_lookup)
    , m_num_banks           (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_banks"))
    , m_num_banks_log2      (floorLog2(m_num_banks))
    , m_num_bank_groups     (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_bank_groups"))
    , m_num_ranks           (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_ranks"))
    , m_rank_offset         (Sim()->getCfg()->getInt("perf_model/dram/ddr/rank_offset"))
    , m_num_channels        (Sim()->getCfg()->getInt("perf_model/dram/ddr/num_channels"))
    , m_channel_offset      (Sim()->getCfg()->getInt("perf_model/dram/ddr/channel_offset"))
    , m_home_lookup_bit     (Sim()->getCfg()->getInt("perf_model/dram_directory/home_lookup_param"))
    , m_total_ranks         (m_num_ranks * m_num_channels)
    , m_banks_per_channel   (m_num_banks * m_num_ranks)
    , m_banks_per_bank_group (m_num_banks / m_num_bank_groups)
    , m_total_banks         (m_banks_per_channel * m_num_channels)
    , m_total_bank_groups   (m_num_bank_groups * m_num_ranks * m_num_channels)
    , m_data_bus_width      (Sim()->getCfg()->getInt("perf_model/dram/ddr/data_bus_width"))   // In bits
    , m_dram_speed          (Sim()->getCfg()->getInt("perf_model/dram/ddr/dram_speed"))       // In MHz
    , m_dram_page_size      (Sim()->getCfg()->getInt("perf_model/dram/ddr/dram_page_size"))
    , m_dram_page_size_log2 (floorLog2(m_dram_page_size))
    , m_open_page_mapping   (Sim()->getCfg()->getBool("perf_model/dram/ddr/open_page_mapping"))
    , m_column_offset       (Sim()->getCfg()->getInt("perf_model/dram/ddr/column_offset"))
    , m_column_hi_offset    (m_dram_page_size_log2 - m_column_offset + m_num_banks_log2) // Offset for higher order column bits
    , m_bank_offset         (m_dram_page_size_log2 - m_column_offset) // Offset for bank bits
    , m_randomize_address   (Sim()->getCfg()->getBool("perf_model/dram/ddr/randomize_address"))
    , m_randomize_offset    (Sim()->getCfg()->getInt("perf_model/dram/ddr/randomize_offset"))
    , m_column_bits_shift   (Sim()->getCfg()->getInt("perf_model/dram/ddr/column_bits_shift"))
    , m_dram_hw_fixed_latency  (SubsecondTime::NS(Sim()->getCfg()->getInt("perf_model/dram/dram_hw_fixed_latency")))
    , m_bus_bandwidth       (m_dram_speed * m_data_bus_width / 1000) // In bits/ns: MT/s=transfers/us * bits/transfer
    , m_r_dram_bus_bandwidth  (m_dram_speed * m_data_bus_width * Sim()->getCfg()->getInt("perf_model/dram/remote_dram_bus_scalefactor") / 1000) // In bits/ns: MT/s=transfers/us * bits/transfer
    , m_r_bus_bandwidth     (m_dram_speed * m_data_bus_width / (1000 * Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_bw_scalefactor"))) // Remote memory
    , m_r_part_bandwidth    (m_dram_speed * m_data_bus_width / (1000 * Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_bw_scalefactor") / (1 - Sim()->getCfg()->getFloat("perf_model/dram/remote_cacheline_queue_fraction")))) // Remote memory - Partitioned Queues => Page Queue
    , m_r_part2_bandwidth   (m_dram_speed * m_data_bus_width / (1000 * Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_bw_scalefactor") / Sim()->getCfg()->getFloat("perf_model/dram/remote_cacheline_queue_fraction"))) // Remote memory - Partitioned Queues => Cacheline Queue
    , m_r_bw_scalefactor    (Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_bw_scalefactor"))
    , m_use_dynamic_bandwidth (Sim()->getCfg()->getBool("perf_model/dram/use_dynamic_bandwidth"))
    , m_use_dynamic_latency (Sim()->getCfg()->getBool("perf_model/dram/use_dynamic_latency"))
    , m_bank_keep_open      (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/bank_keep_open")))
    , m_bank_open_delay     (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/bank_open_delay")))
    , m_bank_close_delay    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/bank_close_delay")))
    , m_dram_access_cost    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/access_cost")))
    , m_r_added_dram_access_cost    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/added_dram_access_cost")))
    , m_intercommand_delay  (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/intercommand_delay"))) // Rank availability
    , m_intercommand_delay_short  (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/intercommand_delay_short"))) // Rank availability
    , m_intercommand_delay_long  (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/intercommand_delay_long"))) // Bank group availability
    , m_controller_delay    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/controller_delay"))) // Average pipeline delay for various DDR controller stages
    , m_refresh_interval    (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/refresh_interval"))) // tREFI
    , m_refresh_length      (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/ddr/refresh_length"))) // tRFC
    , m_r_added_latency       (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_add_lat"))) // Network latency for remote DRAM access 
    , m_r_added_latency_int       (static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/remote_mem_add_lat"))) // Network latency for remote DRAM access 
    , m_r_datamov_threshold       (Sim()->getCfg()->getInt("perf_model/dram/remote_datamov_threshold"))// Move data if greater than
    , m_cache_line_size     (cache_block_size)
    , m_page_size        (Sim()->getCfg()->getInt("perf_model/dram/page_size"))  // Memory page size (bytes) in disagg.cc used for page movement (different from ddr page size)
    , m_localdram_size       (Sim()->getCfg()->getInt("perf_model/dram/localdram_size")) // Local DRAM size
    , m_enable_remote_mem       (Sim()->getCfg()->getBool("perf_model/dram/enable_remote_mem")) // Enable remote memory simulation
    , m_r_simulate_tlb_overhead       (Sim()->getCfg()->getBool("perf_model/dram/simulate_tlb_overhead")) // Simulate TLB overhead
    , m_r_simulate_datamov_overhead       (Sim()->getCfg()->getBool("perf_model/dram/simulate_datamov_overhead"))  // Simulate datamovement overhead for remote DRAM (default: true)
    , m_r_mode       (Sim()->getCfg()->getInt("perf_model/dram/remote_memory_mode")) // Various modes for Local DRAM usage
    , m_r_partitioning_ratio       (Sim()->getCfg()->getInt("perf_model/dram/remote_partitioning_ratio"))// Move data if greater than
    , m_r_simulate_sw_pagereclaim_overhead       (Sim()->getCfg()->getBool("perf_model/dram/simulate_sw_pagereclaim_overhead"))// Add 2600usec delay
    , m_r_exclusive_cache       (Sim()->getCfg()->getBool("perf_model/dram/remote_exclusive_cache"))
    , m_remote_init       (Sim()->getCfg()->getBool("perf_model/dram/remote_init")) // If true, all pages are initially allocated to remote memory
    , m_r_disturbance_factor      (Sim()->getCfg()->getInt("perf_model/dram/remote_disturbance_factor")) // Other systems using the remote memory and creating disturbance
    , m_r_dontevictdirty      (Sim()->getCfg()->getBool("perf_model/dram/remote_dontevictdirty")) // Do not evict dirty pages
    , m_r_enable_selective_moves      (Sim()->getCfg()->getBool("perf_model/dram/remote_enable_selective_moves"))
    , m_r_partition_queues     (Sim()->getCfg()->getInt("perf_model/dram/remote_partitioned_queues")) // Enable partitioned queues
    , m_r_cacheline_queue_fraction    (Sim()->getCfg()->getFloat("perf_model/dram/remote_cacheline_queue_fraction")) // The fraction of remote bandwidth used for the cacheline queue (decimal between 0 and 1)
    , m_use_dynamic_cl_queue_fraction_adjustment    (Sim()->getCfg()->getBool("perf_model/dram/use_dynamic_cacheline_queue_fraction_adjustment"))  // Whether to dynamically adjust m_r_cacheline_queue_fraction
    , m_r_cacheline_gran      (Sim()->getCfg()->getBool("perf_model/dram/remote_use_cacheline_granularity")) // Move data and operate in cacheline granularity
    , m_r_reserved_bufferspace      (Sim()->getCfg()->getFloat("perf_model/dram/remote_reserved_buffer_space")) // Max % of local DRAM that can be reserved for pages in transit
    , m_r_limit_redundant_moves      (Sim()->getCfg()->getInt("perf_model/dram/remote_limit_redundant_moves"))
    , m_r_throttle_redundant_moves      (Sim()->getCfg()->getBool("perf_model/dram/remote_throttle_redundant_moves"))
    , m_r_use_separate_queue_model      (Sim()->getCfg()->getBool("perf_model/dram/queue_model/use_separate_remote_queue_model")) // Whether to use the separate remote queue model
    , m_r_page_queue_utilization_threshold   (Sim()->getCfg()->getFloat("perf_model/dram/remote_page_queue_utilization_threshold")) // When the datamovement queue for pages has percentage utilization above this, remote pages aren't moved to local
    // , m_r_cacheline_queue_utilization_threshold   (Sim()->getCfg()->getFloat("perf_model/dram/remote_cacheline_queue_utilization_threshold")) // When the datamovement queue for cachelines has percentage utilization above this, cacheline requests on inflight pages aren't made
    , m_r_cacheline_queue_utilization_threshold(0.99)
    , m_use_throttled_pages_tracker    (Sim()->getCfg()->getBool("perf_model/dram/use_throttled_pages_tracker"))  // Whether to use and update m_use_throttled_pages_tracker
    , m_use_ideal_page_throttling    (Sim()->getCfg()->getBool("perf_model/dram/r_use_ideal_page_throttling"))  // Whether to use ideal page throttling (alternative currently is FCFS throttling)
    , m_r_ideal_pagethrottle_remote_access_history_window_size   (SubsecondTime::NS() * static_cast<uint64_t> (Sim()->getCfg()->getFloat("perf_model/dram/r_ideal_pagethrottle_access_history_window_size")))
    , m_track_page_bw_utilization_stats    (Sim()->getCfg()->getBool("perf_model/dram/track_page_bw_utilization_stats"))  // Whether to track page queue bw utilization stats
    , m_speed_up_simulation    (Sim()->getCfg()->getBool("perf_model/dram/speed_up_disagg_simulation"))  // When this is true, some optional stats aren't calculated
    , m_r_pq_cacheline_hw_no_queue_delay    (Sim()->getCfg()->getBool("perf_model/dram/r_cacheline_hw_no_queue_delay"))  // When this is true, remove HW access queue delay from PQ=on cacheline requests' critical path to simulate prioritized cachelines
    , m_banks               (m_total_banks)
    , m_r_banks               (m_total_banks)
    , m_inflight_page_delayed(0)
    , m_inflight_pages_delay_time(SubsecondTime::Zero())
    , page_usage_count_stats(m_page_usage_stats_num_points, 0)
    , m_num_recent_remote_accesses(0)
    , m_num_recent_remote_additional_accesses(0)
    , m_num_recent_local_accesses(0)
    // , m_recent_access_count_begin_time(SubsecondTime::Zero())
    , m_r_cacheline_queue_fraction_increased(0)
    , m_r_cacheline_queue_fraction_decreased(0)
    , m_num_accesses(0)
    , m_dram_page_hits           (0)
    , m_dram_page_empty          (0)
    , m_dram_page_closing        (0)
    , m_dram_page_misses         (0)
    , m_local_reads_remote_origin(0)
    , m_local_writes_remote_origin(0)
    , m_remote_reads        (0)
    , m_remote_writes       (0)
    , m_page_moves          (0)
    , m_page_prefetches     (0)
    , m_prefetch_page_not_done_datamovement_queue_full(0)
    , m_prefetch_page_not_done_page_local_already(0)
    , m_prefetch_page_not_done_page_not_initialized(0)
    , m_inflight_hits       (0)
    , m_writeback_pages     (0)
    , m_local_evictions     (0)
    , m_extra_pages         (0)
    , m_redundant_moves     (0)
    , m_redundant_moves_type1  (0)
    , partition_queues_cacheline_slower_than_page (0)  // with the new change, these situations no longer result in redundant moves
    , m_redundant_moves_type2  (0)
    , m_redundant_moves_type2_cancelled_datamovement_queue_full(0)
    , m_redundant_moves_type2_cancelled_limit_redundant_moves(0)
    , m_redundant_moves_type2_slower_than_page_arrival(0)
    , m_max_total_bufferspace(0)
    , m_max_inflight_bufferspace(0)
    , m_max_inflight_extra_bufferspace(0)
    , m_max_inflightevicted_bufferspace(0)
    , m_move_page_cancelled_bufferspace_full(0)
    , m_move_page_cancelled_datamovement_queue_full(0)
    , m_move_page_cancelled_rmode5(0)
    , m_bufferspace_full_page_still_moved(0)
    , m_datamovement_queue_full_page_still_moved(0)
    , m_rmode5_page_moved_due_to_threshold(0)
    , m_unique_pages_accessed      (0)
    , m_ideal_page_throttling_swaps_inflight(0)
    , m_ideal_page_throttling_swaps_non_inflight(0)
    , m_ideal_page_throttling_swap_unavailable(0)
    , m_redundant_moves_type1_time_savings(SubsecondTime::Zero())
    , m_redundant_moves_type2_time_savings(SubsecondTime::Zero())
    , m_total_queueing_delay(SubsecondTime::Zero())
    , m_total_access_latency(SubsecondTime::Zero())
    , m_total_local_access_latency(SubsecondTime::Zero())
    , m_total_remote_access_latency(SubsecondTime::Zero())
    , m_total_remote_datamovement_latency(SubsecondTime::Zero())
    , m_total_local_dram_hardware_latency(SubsecondTime::Zero())
    , m_total_remote_dram_hardware_latency_cachelines(SubsecondTime::Zero())
    , m_total_remote_dram_hardware_latency_pages(SubsecondTime::Zero())
    , m_total_local_dram_hardware_latency_count(0)
    , m_total_remote_dram_hardware_latency_cachelines_count(0)
    , m_total_remote_dram_hardware_latency_pages_count(0)
    , m_total_local_dram_hardware_write_latency_pages(SubsecondTime::Zero())
    // , m_local_get_dram_access_cost_called(0)
    // , m_remote_cacheline_get_dram_access_cost_called(0)
    // , m_remote_page_get_dram_access_cost_called(0)
    , m_local_get_dram_access_cost_processing_time(SubsecondTime::Zero())
    , m_local_get_dram_access_cost_queue_delay(SubsecondTime::Zero())
    , m_remote_cacheline_get_dram_access_cost_processing_time(SubsecondTime::Zero())
    , m_remote_cacheline_get_dram_access_cost_queue_delay(SubsecondTime::Zero())
    , m_remote_page_get_dram_access_cost_processing_time(SubsecondTime::Zero())
    , m_remote_page_get_dram_access_cost_queue_delay(SubsecondTime::Zero())
    , m_cacheline_network_processing_time(SubsecondTime::Zero())
    , m_cacheline_network_queue_delay(SubsecondTime::Zero())
    , m_page_network_processing_time(SubsecondTime::Zero())
    , m_page_network_queue_delay(SubsecondTime::Zero())
    , m_remote_to_local_cacheline_move_count(0)
    , m_remote_to_local_page_move_count(0)
    , m_global_time_much_larger_than_page_arrival(0)
    , m_sum_global_time_much_larger(SubsecondTime::Zero())
    , m_local_total_remote_access_latency(SubsecondTime::Zero())
{
    String name("dram"); 
    if (Sim()->getCfg()->getBool("perf_model/dram/queue_model/enabled"))
    {
        for(UInt32 channel = 0; channel < m_num_channels; ++channel) {
            m_queue_model.push_back(QueueModel::create(
                        name + "-queue-" + itostr(channel), core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                        m_bus_bandwidth.getRoundedLatency(8))); // bytes to bits
            m_r_queue_model.push_back(QueueModel::create(
                        name + "-remote-queue-" + itostr(channel), core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                        m_bus_bandwidth.getRoundedLatency(8))); // bytes to bits
        } 
    }
    m_dram_queue_model_single = QueueModel::create(
                name + "-single-queue", core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                m_bus_bandwidth.getRoundedLatency(8), m_bus_bandwidth.getBandwidthBitsPerUs());
    m_r_dram_queue_model_single = QueueModel::create(
                name + "-single-remote-queue", core_id, Sim()->getCfg()->getString("perf_model/dram/queue_model/type"),
                m_bus_bandwidth.getRoundedLatency(8), m_bus_bandwidth.getBandwidthBitsPerUs());

    String data_movement_queue_model_type;
    if (m_r_use_separate_queue_model) {
        data_movement_queue_model_type = Sim()->getCfg()->getString("perf_model/dram/queue_model/remote_queue_model_type");
    } else {
        data_movement_queue_model_type = Sim()->getCfg()->getString("perf_model/dram/queue_model/type");
    }
    if (m_r_partition_queues == 1) {
        m_data_movement = QueueModel::create(
                name + "-datamovement-queue", core_id, data_movement_queue_model_type,
                m_r_part_bandwidth.getRoundedLatency(8), m_r_part_bandwidth.getBandwidthBitsPerUs()); // bytes to bits
        m_data_movement_2 = QueueModel::create(
                name + "-datamovement-queue-2", core_id, data_movement_queue_model_type,
                m_r_part2_bandwidth.getRoundedLatency(8), m_r_part2_bandwidth.getBandwidthBitsPerUs()); // bytes to bits
    } else if (m_r_partition_queues == 2) {
        // Hardcode the queue model type for now
        m_data_movement = QueueModel::create(
                name + "-datamovement-queue", core_id, "windowed_mg1_remote_ind_queues_enhanced",
                m_r_bus_bandwidth.getRoundedLatency(8), m_r_bus_bandwidth.getBandwidthBitsPerUs(),
                m_r_bus_bandwidth.getRoundedLatency(8*m_page_size), m_r_bus_bandwidth.getRoundedLatency(8*m_cache_line_size)); // bytes to bits
    } else if (m_r_partition_queues == 3) {
        // Hardcode the queue model type for now
        m_data_movement = QueueModel::create(
                name + "-datamovement-queue", core_id, "windowed_mg1_remote_subqueuemodels",
                m_r_bus_bandwidth.getRoundedLatency(8), m_r_bus_bandwidth.getBandwidthBitsPerUs()); // bytes to bits
    } else if (m_r_partition_queues == 4) {
        // Hardcode the queue model type for now
        m_data_movement = QueueModel::create(
                name + "-datamovement-queue", core_id, "windowed_mg1_remote_ind_queues",
                m_r_bus_bandwidth.getRoundedLatency(8), m_r_bus_bandwidth.getBandwidthBitsPerUs()); // bytes to bits
    } else {
        m_data_movement = QueueModel::create(
                name + "-datamovement-queue", core_id, data_movement_queue_model_type,
                m_r_bus_bandwidth.getRoundedLatency(8), m_r_bus_bandwidth.getBandwidthBitsPerUs()); // bytes to bits
        // Note: currently m_data_movement_2 is not used anywhere when m_r_partition_queues == 0
        // m_data_movement_2 = QueueModel::create(
        //         name + "-datamovement-queue-2", core_id, data_movement_queue_model_type,
        //         m_r_bus_bandwidth.getRoundedLatency(8), m_r_bus_bandwidth.getBandwidthBitsPerUs()); // bytes to bits	
    }

    for(UInt32 rank = 0; rank < m_total_ranks; ++rank) {
        m_rank_avail.push_back(QueueModel::create(
                    name + "-rank-" + itostr(rank), core_id, "history_list",
                    (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay));
        m_r_rank_avail.push_back(QueueModel::create(
                    name + "-remote-rank-" + itostr(rank), core_id, "history_list",
                    (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay));
    }

    for(UInt32 group = 0; group < m_total_bank_groups; ++group) {
        m_bank_group_avail.push_back(QueueModel::create(
                    name + "-bank-group-" + itostr(group), core_id, "history_list",
                    m_intercommand_delay_long));
        m_r_bank_group_avail.push_back(QueueModel::create(
                    name + "-remote-bank-group-" + itostr(group), core_id, "history_list",
                    m_intercommand_delay_long));
    }

    if (m_r_mode == 5) {
        m_r_mode_5_limit_moves_threshold = Sim()->getCfg()->getFloat("perf_model/dram/r_mode_5_page_queue_utilization_mode_switch_threshold");
        m_r_mode_5_remote_access_history_window_size = SubsecondTime::NS(Sim()->getCfg()->getInt("perf_model/dram/r_mode_5_remote_access_history_window_size"));
        registerStatsMetric("dram", core_id, "rmode5-move-page-cancelled", &m_move_page_cancelled_rmode5);
        registerStatsMetric("dram", core_id, "rmode5-page-moved-due-to-threshold", &m_rmode5_page_moved_due_to_threshold);
    }

    // Compression
    m_use_compression = Sim()->getCfg()->getBool("perf_model/dram/compression_model/use_compression");
    m_use_cacheline_compression = Sim()->getCfg()->getBool("perf_model/dram/compression_model/cacheline/use_cacheline_compression");
    m_use_r_compressed_pages = Sim()->getCfg()->getBool("perf_model/dram/compression_model/use_r_compressed_pages");
    if (m_use_compression) {
        String compression_scheme = Sim()->getCfg()->getString("perf_model/dram/compression_model/compression_scheme");
        UInt32 gran_size = m_r_cacheline_gran ? m_cache_line_size : m_page_size;

        m_compression_model = CompressionModel::create("Link Compression Model", core_id, gran_size, m_cache_line_size, compression_scheme);
        registerStatsMetric("compression", core_id, "bytes-saved", &bytes_saved);
        registerStatsMetric("compression", core_id, "total-compression-latency", &m_total_compression_latency);
        registerStatsMetric("compression", core_id, "total-decompression-latency", &m_total_decompression_latency);

        // Cacheline Compression
        if (m_use_cacheline_compression) {
            String cacheline_compression_scheme = Sim()->getCfg()->getString("perf_model/dram/compression_model/cacheline/compression_scheme");
            m_cacheline_compression_model = CompressionModel::create("Cacheline Link Compression Model", core_id, m_cache_line_size, m_cache_line_size, cacheline_compression_scheme);
            registerStatsMetric("compression", core_id, "cacheline-bytes-saved", &cacheline_bytes_saved);
            registerStatsMetric("compression", core_id, "total-cacheline-compression-latency", &m_total_cacheline_compression_latency);
            registerStatsMetric("compression", core_id, "total-cacheline-decompression-latency", &m_total_cacheline_decompression_latency);
        }
    }

    // Prefetcher
    m_r_enable_nl_prefetcher = Sim()->getCfg()->getBool("perf_model/dram/enable_remote_prefetcher");  // Enable prefetcher to prefetch pages from remote DRAM to local DRAM
    if (m_r_enable_nl_prefetcher) {
        m_prefetch_unencountered_pages =  Sim()->getCfg()->getBool("perf_model/dram/prefetcher_model/prefetch_unencountered_pages");
        // When using m_r_cacheline_gran, the prefetcher operates by prefetching cachelines too
        UInt32 prefetch_granularity = m_r_cacheline_gran ? m_cache_line_size : m_page_size;

        // Type of prefetcher checked in PrefetcherModel, additional configs for specific prefetcher types are read there
        m_prefetcher_model = PrefetcherModel::createPrefetcherModel(prefetch_granularity);
    }

    LOG_ASSERT_ERROR(cache_block_size == 64, "Hardcoded for 64-byte cache lines");
    LOG_ASSERT_ERROR(m_column_offset <= m_dram_page_size_log2, "Column offset exceeds bounds!");
    if (m_randomize_address)
        LOG_ASSERT_ERROR(m_num_bank_groups == 4 || m_num_bank_groups == 8, "Number of bank groups incorrect for address randomization!");
    // Probably m_page_size needs to be < or <= 2^32 bytes
    LOG_ASSERT_ERROR(m_page_size > 0 && isPower2(m_page_size) && floorLog2(m_page_size) >= floorLog2(m_cache_line_size), "page_size must be a positive power of 2 (and page_size must be larger than the cacheline size)");

    registerStatsMetric("ddr", core_id, "page-hits", &m_dram_page_hits);
    registerStatsMetric("ddr", core_id, "page-empty", &m_dram_page_empty);
    registerStatsMetric("ddr", core_id, "page-closing", &m_dram_page_closing);
    registerStatsMetric("ddr", core_id, "page-misses", &m_dram_page_misses);
    registerStatsMetric("dram", core_id, "total-access-latency", &m_total_access_latency); // cgiannoula
    registerStatsMetric("dram", core_id, "total-local-access-latency", &m_total_local_access_latency);
    registerStatsMetric("dram", core_id, "total-local-dram-hardware-latency", &m_total_local_dram_hardware_latency);
    registerStatsMetric("dram", core_id, "total-local-dram-hardware-latency-processing-time", &m_local_get_dram_access_cost_processing_time);
    registerStatsMetric("dram", core_id, "total-local-dram-hardware-latency-queue-delay", &m_local_get_dram_access_cost_queue_delay);
    registerStatsMetric("dram", core_id, "total-local-dram-hardware-latency-count", &m_total_local_dram_hardware_latency_count);
    registerStatsMetric("dram", core_id, "total-remote-access-latency", &m_total_remote_access_latency);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-cachelines", &m_total_remote_dram_hardware_latency_cachelines);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-cachelines-processing-time", &m_remote_cacheline_get_dram_access_cost_processing_time);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-cachelines-queue-delay", &m_remote_cacheline_get_dram_access_cost_queue_delay);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-pages", &m_total_remote_dram_hardware_latency_pages);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-pages-processing-time", &m_remote_page_get_dram_access_cost_processing_time);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-pages-queue-delay", &m_remote_page_get_dram_access_cost_queue_delay);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-cachelines-count", &m_total_remote_dram_hardware_latency_cachelines_count);
    registerStatsMetric("dram", core_id, "total-remote-dram-hardware-latency-pages-count", &m_total_remote_dram_hardware_latency_pages_count);
    
    registerStatsMetric("dram", core_id, "total-local-dram-hardware-write-latency-pages", &m_total_local_dram_hardware_write_latency_pages);
    
    registerStatsMetric("dram", core_id, "total-remote-datamovement-latency", &m_total_remote_datamovement_latency);
    registerStatsMetric("dram", core_id, "total-network-cacheline-processing-time", &m_cacheline_network_processing_time);
    registerStatsMetric("dram", core_id, "total-network-cacheline-queue-delay", &m_cacheline_network_queue_delay);
    registerStatsMetric("dram", core_id, "total-remote-to-local-cacheline-move-count", &m_remote_to_local_cacheline_move_count);
    registerStatsMetric("dram", core_id, "total-network-page-processing-time", &m_page_network_processing_time);
    registerStatsMetric("dram", core_id, "total-network-page-queue-delay", &m_page_network_queue_delay);
    registerStatsMetric("dram", core_id, "total-remote-to-local-page-move-count", &m_remote_to_local_page_move_count);
    
    // registerStatsMetric("dram", core_id, "local-reads-remote-origin", &m_local_reads_remote_origin);
    // registerStatsMetric("dram", core_id, "local-writes-remote-origin", &m_local_writes_remote_origin);
    registerStatsMetric("dram", core_id, "remote-reads", &m_remote_reads);
    registerStatsMetric("dram", core_id, "remote-writes", &m_remote_writes);
    registerStatsMetric("dram", core_id, "page-moves", &m_page_moves);
    registerStatsMetric("dram", core_id, "bufferspace-full-move-page-cancelled", &m_move_page_cancelled_bufferspace_full);
    registerStatsMetric("dram", core_id, "queue-full-move-page-cancelled", &m_move_page_cancelled_datamovement_queue_full);
    registerStatsMetric("dram", core_id, "bufferspace-full-page-still-moved", &m_bufferspace_full_page_still_moved);
    registerStatsMetric("dram", core_id, "queue-full-page-still-moved", &m_datamovement_queue_full_page_still_moved);    
    registerStatsMetric("dram", core_id, "inflight-hits", &m_inflight_hits);
    registerStatsMetric("dram", core_id, "writeback-pages", &m_writeback_pages);
    registerStatsMetric("dram", core_id, "local-evictions", &m_local_evictions);
    registerStatsMetric("dram", core_id, "extra-traffic", &m_extra_pages);
    registerStatsMetric("dram", core_id, "redundant-moves", &m_redundant_moves);
    registerStatsMetric("dram", core_id, "max-total-bufferspace", &m_max_total_bufferspace);
    registerStatsMetric("dram", core_id, "max-inflight-bufferspace", &m_max_inflight_bufferspace);
    registerStatsMetric("dram", core_id, "max-inflight-extra-bufferspace", &m_max_inflight_extra_bufferspace);
    registerStatsMetric("dram", core_id, "max-inflightevicted-bufferspace", &m_max_inflightevicted_bufferspace);
    registerStatsMetric("dram", core_id, "page-prefetches", &m_page_prefetches);
    registerStatsMetric("dram", core_id, "queue-full-page-prefetch-not-done", &m_prefetch_page_not_done_datamovement_queue_full);
    registerStatsMetric("dram", core_id, "page-local-already-page-prefetch-not-done", &m_prefetch_page_not_done_page_local_already);
    registerStatsMetric("dram", core_id, "page-not-initialized-page-prefetch-not-done", &m_prefetch_page_not_done_page_not_initialized);
    registerStatsMetric("dram", core_id, "unique-pages-accessed", &m_unique_pages_accessed);     
    registerStatsMetric("dram", core_id, "inflight-page-delayed", &m_inflight_page_delayed);
    registerStatsMetric("dram", core_id, "inflight-page-total-delay-time", &m_inflight_pages_delay_time);

    registerStatsMetric("dram", core_id, "page-movement-num-global-time-much-larger", &m_global_time_much_larger_than_page_arrival);
    registerStatsMetric("dram", core_id, "page-movement-global-time-much-larger-total-time", &m_sum_global_time_much_larger);

    registerStatsMetric("dram", core_id, "local-dram-sum-dirty-write-buffer-size", &m_sum_write_buffer_size);
    registerStatsMetric("dram", core_id, "local-dram-max-dirty-write-buffer-size", &m_max_dirty_write_buffer_size);

    registerStatsMetric("dram", core_id, "cacheline-bw-utilization-sum", &m_cacheline_bw_utilization_sum);
    registerStatsMetric("dram", core_id, "page-bw-utilization-sum", &m_page_bw_utilization_sum);
    registerStatsMetric("dram", core_id, "total-bw-utilization-sum", &m_total_bw_utilization_sum);

    // Stats for partition_queues experiments
    if (m_r_partition_queues) {
        registerStatsMetric("dram", core_id, "redundant-moves-type1", &m_redundant_moves_type1);
        registerStatsMetric("dram", core_id, "pq-cacheline-slower-than-page", &partition_queues_cacheline_slower_than_page);
        registerStatsMetric("dram", core_id, "redundant-moves-type2", &m_redundant_moves_type2);
        registerStatsMetric("dram", core_id, "queue-full-redundant-moves-type2-cancelled", &m_redundant_moves_type2_cancelled_datamovement_queue_full);
        registerStatsMetric("dram", core_id, "limit-redundant-moves-redundant-moves-type2-cancelled", &m_redundant_moves_type2_cancelled_limit_redundant_moves);
        registerStatsMetric("dram", core_id, "cacheline-slower-than-inflight-page-arrival", &m_redundant_moves_type2_slower_than_page_arrival);
        registerStatsMetric("dram", core_id, "redundant-moves-type1-time-savings", &m_redundant_moves_type1_time_savings);
        registerStatsMetric("dram", core_id, "redundant-moves-type2-time-savings", &m_redundant_moves_type2_time_savings);

        if (m_use_dynamic_cl_queue_fraction_adjustment) {
            m_min_r_cacheline_queue_fraction = m_r_cacheline_queue_fraction;
            m_max_r_cacheline_queue_fraction = m_r_cacheline_queue_fraction;
            m_min_r_cacheline_queue_fraction_stat_scaled = (UInt64)(m_min_r_cacheline_queue_fraction * 10000);
            m_max_r_cacheline_queue_fraction_stat_scaled = (UInt64)(m_max_r_cacheline_queue_fraction * 10000);
            registerStatsMetric("dram", core_id, "min-runtime-cl-queue-fraction", &m_min_r_cacheline_queue_fraction_stat_scaled);
            registerStatsMetric("dram", core_id, "max-runtime-cl-queue-fraction", &m_max_r_cacheline_queue_fraction_stat_scaled);
            registerStatsMetric("dram", core_id, "num-cl-queue-fraction-increased", &m_r_cacheline_queue_fraction_increased);
            registerStatsMetric("dram", core_id, "num-cl-queue-fraction-decreased", &m_r_cacheline_queue_fraction_decreased);
        }
    }

    // Stats for ideal page throttling algorithm
    if (m_use_ideal_page_throttling) {
        registerStatsMetric("dram", core_id, "ideal-page-throttling-num-swaps-inflight", &m_ideal_page_throttling_swaps_inflight);
        registerStatsMetric("dram", core_id, "ideal-page-throttling-num-swaps-non-inflight", &m_ideal_page_throttling_swaps_non_inflight);
        registerStatsMetric("dram", core_id, "ideal-page-throttling-num-swap-unavailable", &m_ideal_page_throttling_swap_unavailable);
    }
    // Register stats for page locality
    for (UInt32 i = 0; i < m_page_usage_stats_num_points - 1; ++i) {
        UInt32 percentile = (UInt32)(100.0 / m_page_usage_stats_num_points) * (i + 1);
        String stat_name = "page-access-count-p";
        stat_name += std::to_string(percentile).c_str();
        registerStatsMetric("dram", core_id, stat_name, &(page_usage_count_stats[i]));
    }
    // Make sure last one is p100
    registerStatsMetric("dram", core_id, "page-access-count-p100", &(page_usage_count_stats[m_page_usage_stats_num_points - 1]));

    if (m_track_page_bw_utilization_stats) {
        // Stats for BW utilization
        for (int i = 0; i < 10; i++) {
            m_bw_utilization_decile_to_count[i] = 0;
            registerStatsMetric("dram", core_id, ("bw-utilization-decile-" + std::to_string(i)).c_str(), &m_bw_utilization_decile_to_count[i]);
        }
    }

    // For debugging
    std::cout << "dram_perf_model_disagg_remote_only.cc" << std::endl;
    if (m_r_partition_queues)
        std::cout << "Initial m_r_cacheline_queue_fraction=" << m_r_cacheline_queue_fraction << std::endl;
    std::cout << "Uncompressed cacheline remote dram HW bandwidth processing time= " << m_r_dram_bus_bandwidth.getRoundedLatency(8 * m_cache_line_size).getPS() / 1000.0 << " ns, ";
    std::cout << "Uncompressed page remote dram HW bandwidth processing time= " << m_r_dram_bus_bandwidth.getRoundedLatency(8 * m_page_size).getPS() / 1000.0 << " ns" << std::endl;
    
    // RNG
    srand (time(NULL));
}

DramPerfModelDisaggRemoteOnly::~DramPerfModelDisaggRemoteOnly()
{
    bool print_extra_stats = Sim()->getCfg()->getBool("perf_model/dram/print_extra_stats");
    if (print_extra_stats) {
        // Remote Avg Latency Stats
        std::cout << "\nAvg Remote DRAM Access Latencies:\n";
        for (std::vector<SubsecondTime>::iterator it = m_local_total_remote_access_latency_avgs.begin(); it != m_local_total_remote_access_latency_avgs.end(); ++it) {
            UInt64 local_remote_access_latency_avg = it->getNS();
            std::cout << local_remote_access_latency_avg << ' ';
        }
        std::cout << "\n\n";

        // Local IPC Stats
        std::cout << "\nLocal IPC:\n";
        for (std::vector<double>::iterator it = m_local_IPCs.begin(); it != m_local_IPCs.end(); ++it) {
            double local_IPC = *it;
            std::cout << local_IPC << ' ';
        }
        std::cout << "\n\n";
    }

    if (m_queue_model.size())
    {
        for(UInt32 channel = 0; channel < m_num_channels; ++channel)
            delete m_queue_model[channel];
    }
    delete m_data_movement;

    if (m_rank_avail.size())
    {
        for(UInt32 rank = 0; rank < m_total_ranks; ++rank)
            delete m_rank_avail[rank];
    }

    if (m_bank_group_avail.size())
    {
        for(UInt32 group = 0; group < m_total_bank_groups; ++group)
            delete m_bank_group_avail[group];
    }
    if (m_r_queue_model.size())
    {
        for(UInt32 channel = 0; channel < m_num_channels; ++channel)
            delete m_r_queue_model[channel];
    }

    if (m_r_rank_avail.size())
    {
        for(UInt32 rank = 0; rank < m_total_ranks; ++rank)
            delete m_r_rank_avail[rank];
    }

    if (m_r_bank_group_avail.size())
    {
        for(UInt32 group = 0; group < m_total_bank_groups; ++group)
            delete m_r_bank_group_avail[group];
    }

    // putting this near the end so hopefully print output from the two queue model destructors won't interfere
    if (m_r_partition_queues == 1) {
        delete m_data_movement_2;
    }

    if (m_r_partition_queues) {
        std::cout << "Final m_r_cacheline_queue_fraction=" << m_r_cacheline_queue_fraction << std::endl;
        std::cout << "Final m_r_cacheline_queue_fraction_increased=" << m_r_cacheline_queue_fraction_increased << ", Final m_r_cacheline_queue_fraction_decreased=" << m_r_cacheline_queue_fraction_decreased << std::endl;
        std::cout << "Final m_min_r_cacheline_queue_fraction_stat_scaled=" << m_min_r_cacheline_queue_fraction_stat_scaled << ", Final m_max_r_cacheline_queue_fraction_stat_scaled=" << m_max_r_cacheline_queue_fraction_stat_scaled << std::endl;
    }
}

// Modifies input vector by sorting it
template<typename T>
void DramPerfModelDisaggRemoteOnly::sortAndPrintVectorPercentiles(std::vector<T>& vec, std::ostringstream& percentages_buffer, std::ostringstream& counts_buffer, UInt32 num_bins)
{
    std::sort(vec.begin(), vec.end());

    UInt64 index;
    T percentile;
    double percentage;
    
    // Compute individual_counts percentiles for output
    // the total number of points is 1 more than num_bins, since it includes the endpoints
    std::map<double, T> percentiles;
    for (UInt32 bin = 0; bin < num_bins; ++bin) {
        percentage = (double)bin / num_bins;
        index = (UInt64)(percentage * (vec.size() - 1));  // -1 so array indices don't go out of bounds
        percentile = vec[index];
        std::cout << "percentage: " << percentage << ", vector index: " << index << ", percentile: " << percentile << std::endl;
        percentiles.insert(std::pair<double, T>(percentage, percentile));
    }
    // Add the maximum
    percentage = 1.0;
    percentile = vec[vec.size() - 1];
    std::cout << "percentage: " << percentage << ", vector index: " << vec.size() - 1 << ", percentile: " << percentile << std::endl;
    percentiles.insert(std::pair<double, T>(percentage, percentile));
    // Print output in format that can easily be graphed in Python
    percentages_buffer << "[";
    counts_buffer << "[";
    for (auto it = percentiles.begin(); it != percentiles.end(); ++it) {
        percentages_buffer << it->first << ", ";
        counts_buffer << it->second << ", ";
    }
    percentages_buffer << "]";
    counts_buffer << "]";
}

void
DramPerfModelDisaggRemoteOnly::finalizeStats() 
{
    bool process_and_print_page_locality_stats = true;
    bool process_and_print_throttled_pages_stats = true;
    if (m_speed_up_simulation) {
        process_and_print_page_locality_stats = false;
        process_and_print_throttled_pages_stats = false;
    }

    // Finalize queue model stats
    m_data_movement->finalizeStats();
    if (m_r_partition_queues == 1) {
        m_data_movement_2->finalizeStats();
    }
    // Compression Stats
    if (m_use_compression) {
        m_compression_model->finalizeStats();
        if (m_use_cacheline_compression)
            ;//m_cacheline_compression_model->finalizeStats();
    }

    std::cout << "dram_perf_model_disagg.cc finalizeStats():" << std::endl;
    if (m_page_locality_measures.size() > 0) {
        std::cout << "True page locality measure:" << std::endl;
        std::ostringstream percentages_buf;
        std::ostringstream counts_buf;
        sortAndPrintVectorPercentiles(m_page_locality_measures, percentages_buf, counts_buf);
        std::cout << "CDF X values (true page locality):\n" << counts_buf.str() << std::endl;
        std::cout << "CDF Y values (probability):\n" << percentages_buf.str() << std::endl;
    }
    if (m_modified_page_locality_measures.size() > 0) {
        std::cout << "Modified page locality measure:" << std::endl;
        std::ostringstream percentages_buf;
        std::ostringstream counts_buf;
        sortAndPrintVectorPercentiles(m_modified_page_locality_measures, percentages_buf, counts_buf);
        std::cout << "CDF X values (modified page locality):\n" << counts_buf.str() << std::endl;
        std::cout << "CDF Y values (probability):\n" << percentages_buf.str() << std::endl;
    }
    if (m_modified2_page_locality_measures.size() > 0) {
        std::cout << "Modified2 page locality measure:" << std::endl;
        std::ostringstream percentages_buf;
        std::ostringstream counts_buf;
        sortAndPrintVectorPercentiles(m_modified2_page_locality_measures, percentages_buf, counts_buf);
        std::cout << "CDF X values (modified2 page locality):\n" << counts_buf.str() << std::endl;
        std::cout << "CDF Y values (probability):\n" << percentages_buf.str() << std::endl;
    }

    if (process_and_print_page_locality_stats && m_page_usage_map.size() > 0) {
        // Put values in a vector and sort
        std::vector<std::pair<UInt32, UInt64>> page_usage_counts;  // first is access count, second is the phys_page
        for (auto it = m_page_usage_map.begin(); it != m_page_usage_map.end(); ++it) {
            page_usage_counts.push_back(std::pair<UInt32, UInt64>(it->second, it->first));
        }
        std::sort(page_usage_counts.begin(), page_usage_counts.end());  // sort by access count

        // Update stats vector
        for (UInt32 i = 0; i < m_page_usage_stats_num_points - 1; ++i) {
            UInt64 index = (UInt64)((double)(i + 1) / m_page_usage_stats_num_points * (page_usage_counts.size() - 1));
            page_usage_count_stats[i] = page_usage_counts[index].first;
        }
        // Make sure last one is p100
        page_usage_count_stats[m_page_usage_stats_num_points - 1] = page_usage_counts[page_usage_counts.size() - 1].first;

        // Compute individual_counts percentiles for output
        UInt64 index;
        UInt32 percentile;
        double percentage;
        std::cout << "dram_perf_model_disagg.cc:" << std::endl;
        std::cout << "Page usage counts:" << std::endl;
        UInt32 num_bins = 40;  // the total number of points is 1 more than num_bins, since it includes the endpoints
        std::map<double, UInt32> usage_counts_percentiles;
        for (UInt32 bin = 0; bin < num_bins; ++bin) {
            percentage = (double)bin / num_bins;
            index = (UInt64)(percentage * (page_usage_counts.size() - 1));  // -1 so array indices don't go out of bounds
            percentile = page_usage_counts[index].first;
            std::cout << "percentage: " << percentage << ", vector index: " << index << ", percentile: " << percentile << std::endl;
            usage_counts_percentiles.insert(std::pair<double, UInt32>(percentage, percentile));
        }
        // Add the maximum
        percentage = 1.0;
        percentile = page_usage_counts[page_usage_counts.size() - 1].first;
        std::cout << "percentage: " << percentage << ", vector index: " << page_usage_counts.size() - 1 << ", percentile: " << percentile << std::endl;
        usage_counts_percentiles.insert(std::pair<double, UInt32>(percentage, percentile));
        // Print output in format that can easily be graphed in Python
        std::ostringstream percentages_buffer;
        std::ostringstream cdf_buffer_usage_counts;
        percentages_buffer << "[";
        cdf_buffer_usage_counts << "[";
        for (std::map<double, UInt32>::iterator it = usage_counts_percentiles.begin(); it != usage_counts_percentiles.end(); ++it) {
            percentages_buffer << it->first << ", ";
            cdf_buffer_usage_counts << it->second << ", ";
        }
        percentages_buffer << "]";
        cdf_buffer_usage_counts << "]";

        std::cout << "CDF X values (page usage counts):\n" << cdf_buffer_usage_counts.str() << std::endl;
        std::cout << "CDF Y values (probability):\n" << percentages_buffer.str() << std::endl;

        // // Print the least accessed 1/3 of phys_pages
        // std::ostringstream least_accessed_phys_pages_buffer;
        // least_accessed_phys_pages_buffer << "{ ";
        // std::vector<std::pair<UInt32, UInt64>>::iterator it = page_usage_counts.begin();
        // for (UInt64 i = 0; i < page_usage_counts.size() / 3; ++i) {
        //     least_accessed_phys_pages_buffer << it->second << ", ";
        //     ++it;
        // }
        // least_accessed_phys_pages_buffer << " }";
        // std::cout << "Least accessed phys_pages:\n" << least_accessed_phys_pages_buffer.str() << std::endl;

    }
    if (process_and_print_throttled_pages_stats && m_use_throttled_pages_tracker) {
        // Add values in the map at the end of program execution to m_throttled_pages_tracker_values
        for (std::map<UInt64, std::pair<SubsecondTime, UInt32>>::iterator it = m_throttled_pages_tracker.begin(); it != m_throttled_pages_tracker.end(); ++it) {
            // if ((it->second).second > 0)
            m_throttled_pages_tracker_values.push_back(std::pair<UInt64, UInt32>(it->first, (it->second).second));
        }
        if (m_throttled_pages_tracker_values.size() < 1) {
            return;
        }

        // Build vectors of the stats we want
        std::vector<UInt32> throttled_pages_tracker_individual_counts;  // counts for the same phys_page are separate values
        std::map<UInt64, UInt32> throttled_pages_tracker_page_aggregated_counts_map;  // temporary variable
        std::vector<UInt32> throttled_pages_tracker_page_aggregated_counts;  // aggregate all numbers for a particular phys_page together
        for (std::vector<std::pair<UInt64, UInt32>>::iterator it = m_throttled_pages_tracker_values.begin(); it != m_throttled_pages_tracker_values.end(); ++it) {
            UInt64 phys_page = it->first;
            UInt32 count = it->second;

            throttled_pages_tracker_individual_counts.push_back(count);  // update individual counts

            if (!throttled_pages_tracker_page_aggregated_counts_map.count(phys_page)) {  // update aggregated counts map
                throttled_pages_tracker_page_aggregated_counts_map[phys_page] = 0;
            }
            throttled_pages_tracker_page_aggregated_counts_map[phys_page] += count;
        }
        // Generate aggregated counts vector
        for (std::map<UInt64, UInt32>::iterator it = throttled_pages_tracker_page_aggregated_counts_map.begin(); it != throttled_pages_tracker_page_aggregated_counts_map.end(); ++it) {
            throttled_pages_tracker_page_aggregated_counts.push_back(it->second);
        }
        
        // Compute individual_counts percentiles for output
        std::cout << "Throttled pages tracker individual counts:" << std::endl;
        std::ostringstream percentages_buf;
        std::ostringstream individual_counts_buf;
        sortAndPrintVectorPercentiles(throttled_pages_tracker_individual_counts, percentages_buf, individual_counts_buf);
        std::cout << "CDF X values (throttled page accesses within time frame):\n" << individual_counts_buf.str() << std::endl;
        std::cout << "CDF Y values (probability):\n" << percentages_buf.str() << std::endl;

        // Compute aggregated_counts percentiles for output
        std::cout << "Throttled pages tracker page aggregated counts:" << std::endl;
        std::ostringstream percentages_buf_2;
        std::ostringstream aggregated_counts_buf;
        sortAndPrintVectorPercentiles(throttled_pages_tracker_page_aggregated_counts, percentages_buf_2, aggregated_counts_buf);
        std::cout << "CDF X values (throttled page accesses aggregated by phys_page):\n" << aggregated_counts_buf.str() << std::endl;
        std::cout << "CDF Y values (probability):\n" << percentages_buf_2.str() << std::endl;
    }
}

UInt64
DramPerfModelDisaggRemoteOnly::parseAddressBits(UInt64 address, UInt32 &data, UInt32 offset, UInt32 size, UInt64 base_address = 0)
{
    // Parse data from the address based on the offset and size, return the address without the bits used to parse the data.
    UInt32 log2_size = floorLog2(size);
    if (base_address != 0) {
        data = (base_address >> offset) % size;
    } else {
        data = (address >> offset) % size;
    }
    return ((address >> (offset + log2_size)) << offset) | (address & ((1 << offset) - 1));
}

void
DramPerfModelDisaggRemoteOnly::parseDeviceAddress(IntPtr address, UInt32 &channel, UInt32 &rank, UInt32 &bank_group, UInt32 &bank, UInt32 &column, UInt64 &dram_page)
{
    // Construct DDR address which has bits used for interleaving removed
    UInt64 linearAddress = m_address_home_lookup->getLinearAddress(address);
    UInt64 address_bits = linearAddress >> 6;
    /*address_bits = */parseAddressBits(address_bits, channel, m_channel_offset, m_num_channels, m_channel_offset < m_home_lookup_bit ? address : linearAddress);
    address_bits = parseAddressBits(address_bits, rank,    m_rank_offset,    m_num_ranks,    m_rank_offset < m_home_lookup_bit ? address : linearAddress);


    if (m_open_page_mapping) {
        // Open-page mapping: column address is bottom bits, then bank, then page
        if (m_column_offset) {
            // Column address is split into 2 halves ColHi and ColLo and
            // the address looks like: | Page | ColHi | Bank | ColLo |
            // m_column_offset specifies the number of ColHi bits
            column = (((address_bits >> m_column_hi_offset) << m_bank_offset)
                    | (address_bits & ((1 << m_bank_offset) - 1))) % m_dram_page_size;
            address_bits = address_bits >> m_bank_offset;
            bank_group = address_bits % m_num_bank_groups;
            bank = address_bits % m_num_banks;
            address_bits = address_bits >> (m_num_banks_log2 + m_column_offset);
        } else {
            column = address_bits % m_dram_page_size; address_bits /= m_dram_page_size;
            bank_group = address_bits % m_num_bank_groups;
            bank = address_bits % m_num_banks; address_bits /= m_num_banks;
        }
        dram_page = address_bits;

#if 0
        // Test address parsing done in this function for open page mapping
        std::bitset<10> bs_col (column);
        std::string str_col = bs_col.to_string<char,std::string::traits_type,std::string::allocator_type>();
        std::stringstream ss_original, ss_recomputed;
        ss_original << std::bitset<64>(linearAddress >> m_block_size_log2) << std::endl;
        ss_recomputed << std::bitset<50>(dram_page) << str_col.substr(0,m_column_offset) << std::bitset<4>(bank)
            << str_col.substr(m_column_offset, str_col.length()-m_column_offset) << std::endl;
        LOG_ASSERT_ERROR(ss_original.str() == ss_recomputed.str(), "Error in device address parsing!");
#endif
    } else {

        bank_group = address_bits % m_num_bank_groups;
        bank = address_bits % m_num_banks;
        address_bits /= m_num_banks;

        // Closed-page mapping: column address is bits X+banksize:X, row address is everything else
        // (from whatever is left after cutting channel/rank/bank from the bottom)
        column = (address_bits >> m_column_bits_shift) % m_dram_page_size;
        dram_page = (((address_bits >> m_column_bits_shift) / m_dram_page_size) << m_column_bits_shift)
            | (address_bits & ((1 << m_column_bits_shift) - 1));
    }

    if (m_randomize_address) {
        std::bitset<3> row_bits(dram_page >> m_randomize_offset);                 // Row[offset+2:offset]
        UInt32 row_bits3 = row_bits.to_ulong();
        row_bits[2] = 0;
        UInt32 row_bits2 = row_bits.to_ulong();
        bank_group ^= ((m_num_bank_groups == 8) ? row_bits3 : row_bits2);    // BankGroup XOR Row
        bank /= m_num_bank_groups;
        bank ^= row_bits2;                                                   // Bank XOR Row
        bank = m_banks_per_bank_group * bank_group + bank;
        rank = (m_num_ranks > 1) ? rank ^ row_bits[0] : rank;                // Rank XOR Row
    }

    //printf("[%2d] address %12lx linearAddress %12lx channel %2x rank %2x bank_group %2x bank %2x dram_page %8lx crb %4u\n", m_core_id, address, linearAddress, channel, rank, bank_group, bank, dram_page, (((channel * m_num_ranks) + rank) * m_num_banks) + bank);
}

// DRAM hardware access cost
SubsecondTime
DramPerfModelDisaggRemoteOnly::getDramWriteCost(SubsecondTime start_time, UInt64 size, core_id_t requester, IntPtr address, ShmemPerf *perf, bool is_exclude_cacheline, bool is_page)
{
    // Precondition: this method is called from remote
    SubsecondTime t_now = start_time;
    SubsecondTime dram_access_cost = m_dram_hw_fixed_latency;

    SubsecondTime ddr_processing_time;
    SubsecondTime ddr_queue_delay;
    if (is_exclude_cacheline && is_page) {
        UInt32 num_cachelines_in_page = m_page_size/m_cache_line_size;
        size = ((double)(num_cachelines_in_page - 1))/num_cachelines_in_page * size;
    }
    ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * size); // bytes to bits
    ddr_queue_delay = m_dram_queue_model_single->computeQueueDelay(t_now, ddr_processing_time, requester);

    perf->updateTime(t_now);
    t_now += ddr_queue_delay;
    perf->updateTime(t_now, ShmemPerf::DRAM_QUEUE);
    t_now += ddr_processing_time;
    perf->updateTime(t_now, ShmemPerf::DRAM_BUS);
    t_now += dram_access_cost;
    perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    return t_now - start_time;  // Net increase of time, ie the pure hardware access cost
    // return ddr_queue_delay + ddr_processing_time + dram_access_cost;
}

// DRAM hardware access cost
SubsecondTime
DramPerfModelDisaggRemoteOnly::getDramAccessCost(SubsecondTime start_time, UInt64 size, core_id_t requester, IntPtr address, ShmemPerf *perf, bool is_remote, bool is_exclude_cacheline, bool is_page)
{
    SubsecondTime t_now = start_time;
    SubsecondTime dram_access_cost = m_dram_hw_fixed_latency;

    SubsecondTime ddr_processing_time;
    SubsecondTime ddr_queue_delay;
    if (is_exclude_cacheline && is_page) {
        UInt32 num_cachelines_in_page = m_page_size/m_cache_line_size;
        size = ((double)(num_cachelines_in_page - 1))/num_cachelines_in_page * size;
    }
    if (is_remote) {
        ddr_processing_time = m_r_dram_bus_bandwidth.getRoundedLatency(8 * size); // bytes to bits
        ddr_queue_delay = m_r_dram_queue_model_single->computeQueueDelay(t_now, ddr_processing_time, requester);
        if (is_page) {
            m_remote_page_get_dram_access_cost_processing_time += ddr_processing_time;
            m_remote_page_get_dram_access_cost_queue_delay += ddr_queue_delay;

            // Debug: Add extra dram access cost if page
            t_now += m_r_added_dram_access_cost;
            perf->updateTime(t_now);
        } else {
            // This is a cacheline
            if (m_r_pq_cacheline_hw_no_queue_delay && m_r_partition_queues) {
                ddr_queue_delay = SubsecondTime::Zero();  // option to remove hw queue delay from cachelines when PQ=on, to simulate prioritized cachelines
            }
            m_remote_cacheline_get_dram_access_cost_processing_time += ddr_processing_time;
            m_remote_cacheline_get_dram_access_cost_queue_delay += ddr_queue_delay;
        }
    } else {
        ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * size); // bytes to bits
        ddr_queue_delay = m_dram_queue_model_single->computeQueueDelay(t_now, ddr_processing_time, requester);
        m_local_get_dram_access_cost_processing_time += ddr_processing_time;
        m_local_get_dram_access_cost_queue_delay += ddr_queue_delay;
    }

    perf->updateTime(t_now);
    t_now += ddr_queue_delay;
    perf->updateTime(t_now, ShmemPerf::DRAM_QUEUE);
    t_now += ddr_processing_time;
    perf->updateTime(t_now, ShmemPerf::DRAM_BUS);
    t_now += dram_access_cost;
    perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    return t_now - start_time;  // Net increase of time, ie the pure hardware access cost
    // return ddr_queue_delay + ddr_processing_time + dram_access_cost;



    // UInt64 phys_page = address & ~((UInt64(1) << floorLog2(m_page_size)) - 1);
    // IntPtr cacheline_address = (size > m_cache_line_size) ? phys_page : address & ~((UInt64(1) << floorLog2(m_cache_line_size)) - 1);
    // IntPtr actual_data_cacheline_address = address & ~((UInt64(1) << floorLog2(m_cache_line_size)) - 1);
    // SubsecondTime t_now = start_time;

    // if (is_remote) {
    //     for (UInt32 i = 0; i < size / m_cache_line_size; i++) {
    //         if (is_exclude_cacheline && cacheline_address == actual_data_cacheline_address)
    //             continue;

    //         // Calculate address mapping inside the DIMM
    //         UInt32 channel, rank, bank_group, bank, column;
    //         UInt64 dram_page;
    //         parseDeviceAddress(cacheline_address, channel, rank, bank_group, bank, column, dram_page);

    //         perf->updateTime(t_now);

    //         // Add DDR controller pipeline delay
    //         t_now += m_controller_delay;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_CNTLR);

    //         // Add DDR refresh delay if needed
    //         if (m_refresh_interval != SubsecondTime::Zero()) {

    //             SubsecondTime refresh_base = (t_now.getPS() / m_refresh_interval.getPS()) * m_refresh_interval;
    //             if (t_now - refresh_base < m_refresh_length) {
    //                 t_now = refresh_base + m_refresh_length;
    //                 perf->updateTime(t_now, ShmemPerf::DRAM_REFRESH);
    //             }
    //         }

    //         UInt64 crb = (channel * m_num_ranks * m_num_banks) + (rank * m_num_banks) + bank; // Combine channel, rank, bank to index m_banks
    //         LOG_ASSERT_ERROR(crb < m_total_banks, "Bank index out of bounds");
    //         BankInfo &bank_info = m_r_banks[crb];

    //         //printf("[%2d] %s (%12lx, %4lu, %4lu), t_open = %lu, t_now = %lu, bank_info.t_avail = %lu\n", m_core_id, bank_info.open_page == dram_page && bank_info.t_avail + m_bank_keep_open >= t_now ? "Page Hit: " : "Page Miss:", address, crb, dram_page % 10000, t_now.getNS() - bank_info.t_avail.getNS(), t_now.getNS(), bank_info.t_avail.getNS());
    //         // DRAM page hit/miss
    //         if (bank_info.open_page == dram_page                            // Last access was to this row
    //                 && bank_info.t_avail + m_bank_keep_open >= t_now   // Bank hasn't been closed in the meantime
    //         ) {

    //             if (bank_info.t_avail > t_now) {
    //                 t_now = bank_info.t_avail;
    //                 perf->updateTime(t_now, ShmemPerf::DRAM_BANK_PENDING);
    //             }
    //             ++m_dram_page_hits;

    //         } else {
    //             // Wait for bank to become available
    //             if (bank_info.t_avail > t_now)
    //                 t_now = bank_info.t_avail;
    //             // Close dram_page
    //             if (bank_info.t_avail + m_bank_keep_open >= t_now) {
    //                 // We found the dram_page open and have to close it ourselves
    //                 t_now += m_bank_close_delay;
    //                 ++m_dram_page_misses;
    //             } else if (bank_info.t_avail + m_bank_keep_open + m_bank_close_delay > t_now) {
    //                 // Bank was being closed, we have to wait for that to complete
    //                 t_now = bank_info.t_avail + m_bank_keep_open + m_bank_close_delay;
    //                 ++m_dram_page_closing;
    //             } else {
    //                 // Bank was already closed, no delay.
    //                 ++m_dram_page_empty;
    //             }

    //             // Open dram_page
    //             t_now += m_bank_open_delay;
    //             perf->updateTime(t_now, ShmemPerf::DRAM_BANK_CONFLICT);

    //             bank_info.open_page = dram_page;
    //         }

    //         // Add rank access time and availability
    //         UInt64 cr = (channel * m_num_ranks) + rank;
    //         LOG_ASSERT_ERROR(cr < m_total_ranks, "Rank index out of bounds");
    //         SubsecondTime rank_avail_request = (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay;
    //         SubsecondTime rank_avail_delay = m_r_rank_avail.size() ? m_r_rank_avail[cr]->computeQueueDelay(t_now, rank_avail_request, requester) : SubsecondTime::Zero();

    //         // Add bank group access time and availability
    //         UInt64 crbg = (channel * m_num_ranks * m_num_bank_groups) + (rank * m_num_bank_groups) + bank_group;
    //         LOG_ASSERT_ERROR(crbg < m_total_bank_groups, "Bank-group index out of bounds");
    //         SubsecondTime group_avail_delay = m_r_bank_group_avail.size() ? m_r_bank_group_avail[crbg]->computeQueueDelay(t_now, m_intercommand_delay_long, requester) : SubsecondTime::Zero();

    //         // Add device access time (tCAS)
    //         t_now += m_dram_access_cost;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    //         // Mark bank as busy until it can receive its next command
    //         // Done before waiting for the bus to be free: sort of assumes best-case bus scheduling
    //         bank_info.t_avail = t_now;

    //         // Add the wait time for the larger of bank group and rank availability delay
    //         t_now += (rank_avail_delay > group_avail_delay) ? rank_avail_delay : group_avail_delay;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);
    //         //std::cout << "DDR Processing time: " << m_bus_bandwidth.getRoundedLatency(8*pkt_size) << std::endl;

    //         // Add DDR bus latency and queuing delay
    //         SubsecondTime ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * m_cache_line_size); // bytes to bits
    //         SubsecondTime ddr_queue_delay = m_r_queue_model.size() ? m_r_queue_model[channel]->computeQueueDelay(t_now, ddr_processing_time, requester) : SubsecondTime::Zero();
    //         t_now += ddr_queue_delay;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_QUEUE);

    //         // Get next cacheline address
    //         cacheline_address += (UInt64(1) << floorLog2(m_cache_line_size));
    //     }
    // } else {
    //     for (UInt32 i = 0; i < size / m_cache_line_size; i++) {
    //         if (is_exclude_cacheline && cacheline_address == actual_data_cacheline_address)
    //             continue;

    //         // Calculate address mapping inside the DIMM
    //         UInt32 channel, rank, bank_group, bank, column;
    //         UInt64 dram_page;
    //         parseDeviceAddress(address, channel, rank, bank_group, bank, column, dram_page);

    //         perf->updateTime(t_now);

    //         // Add DDR controller pipeline delay
    //         t_now += m_controller_delay;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_CNTLR);

    //         // Add DDR refresh delay if needed
    //         if (m_refresh_interval != SubsecondTime::Zero()) {
    //             SubsecondTime refresh_base = (t_now.getPS() / m_refresh_interval.getPS()) * m_refresh_interval;
    //             if (t_now - refresh_base < m_refresh_length) {
    //                 t_now = refresh_base + m_refresh_length;
    //                 perf->updateTime(t_now, ShmemPerf::DRAM_REFRESH);
    //             }
    //         }

    //         UInt64 crb = (channel * m_num_ranks * m_num_banks) + (rank * m_num_banks) + bank; // Combine channel, rank, bank to index m_banks
    //         LOG_ASSERT_ERROR(crb < m_total_banks, "Bank index out of bounds");
    //         BankInfo &bank_info = m_banks[crb];

    //         //printf("[%2d] %s (%12lx, %4lu, %4lu), t_open = %lu, t_now = %lu, bank_info.t_avail = %lu\n", m_core_id, bank_info.open_page == dram_page && bank_info.t_avail + m_bank_keep_open >= t_now ? "Page Hit: " : "Page Miss:", address, crb, dram_page % 10000, t_now.getNS() - bank_info.t_avail.getNS(), t_now.getNS(), bank_info.t_avail.getNS());

    //         // DRAM page hit/miss
    //         if (bank_info.open_page == dram_page                       // Last access was to this row
    //                 && bank_info.t_avail + m_bank_keep_open >= t_now   // Bank hasn't been closed in the meantime
    //         ) {
    //             if (bank_info.t_avail > t_now) {
    //                 t_now = bank_info.t_avail;
    //                 perf->updateTime(t_now, ShmemPerf::DRAM_BANK_PENDING);
    //             }
    //             ++m_dram_page_hits;

    //         } else {
    //             // Wait for bank to become available
    //             if (bank_info.t_avail > t_now)
    //                 t_now = bank_info.t_avail;
    //             // Close dram_page
    //             if (bank_info.t_avail + m_bank_keep_open >= t_now) {
    //                 // We found the dram_page open and have to close it ourselves
    //                 t_now += m_bank_close_delay;
    //                 ++m_dram_page_misses;
    //             } else if (bank_info.t_avail + m_bank_keep_open + m_bank_close_delay > t_now) {
    //                 // Bank was being closed, we have to wait for that to complete
    //                 t_now = bank_info.t_avail + m_bank_keep_open + m_bank_close_delay;
    //                 ++m_dram_page_closing;
    //             } else {
    //                 // Bank was already closed, no delay.
    //                 ++m_dram_page_empty;
    //             }

    //             // Open dram_page
    //             t_now += m_bank_open_delay;
    //             perf->updateTime(t_now, ShmemPerf::DRAM_BANK_CONFLICT);

    //             bank_info.open_page = dram_page;
    //         }

    //         // Add rank access time and availability
    //         UInt64 cr = (channel * m_num_ranks) + rank;
    //         LOG_ASSERT_ERROR(cr < m_total_ranks, "Rank index out of bounds");
    //         SubsecondTime rank_avail_request = (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay;
    //         SubsecondTime rank_avail_delay = m_rank_avail.size() ? m_rank_avail[cr]->computeQueueDelay(t_now, rank_avail_request, requester) : SubsecondTime::Zero();

    //         // Add bank group access time and availability
    //         UInt64 crbg = (channel * m_num_ranks * m_num_bank_groups) + (rank * m_num_bank_groups) + bank_group;
    //         LOG_ASSERT_ERROR(crbg < m_total_bank_groups, "Bank-group index out of bounds");
    //         SubsecondTime group_avail_delay = m_bank_group_avail.size() ? m_bank_group_avail[crbg]->computeQueueDelay(t_now, m_intercommand_delay_long, requester) : SubsecondTime::Zero();

    //         // Add device access time (tCAS)
    //         t_now += m_dram_access_cost;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    //         // Mark bank as busy until it can receive its next command
    //         // Done before waiting for the bus to be free: sort of assumes best-case bus scheduling
    //         bank_info.t_avail = t_now;

    //         // Add the wait time for the larger of bank group and rank availability delay
    //         t_now += (rank_avail_delay > group_avail_delay) ? rank_avail_delay : group_avail_delay;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    //         // DDR bus latency and queuing delay
    //         SubsecondTime ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * m_cache_line_size); // bytes to bits
    //         //std::cout << m_bus_bandwidth.getRoundedLatency(8*pkt_size) << std::endl;
    //         SubsecondTime ddr_queue_delay = m_queue_model.size() ? m_queue_model[channel]->computeQueueDelay(t_now, ddr_processing_time, requester) : SubsecondTime::Zero();
    //         t_now += ddr_queue_delay;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_QUEUE);
    //         t_now += ddr_processing_time;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_BUS);

    //         // Get next cacheline address
    //         cacheline_address += (UInt64(1) << floorLog2(m_cache_line_size));
    //     }
    // }

    // return t_now - start_time;  // Net increase of time, ie the pure hardware access cost
}

SubsecondTime
DramPerfModelDisaggRemoteOnly::getAccessLatencyRemote(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf)
{
    // pkt_size is in 'Bytes'
    // m_dram_bandwidth is in 'Bits per clock cycle'

    // When partition queues is off: don't get cacheline when moving the page
    // SubsecondTime cacheline_hw_access_latency = SubsecondTime::Zero();
    // if (m_r_partition_queues) {
    //     // When partition queues is off, access the requested cacheline first
    //     cacheline_hw_access_latency = getDramAccessCost(pkt_time, pkt_size, requester, address, perf, true, false, false);
    //     m_total_remote_dram_hardware_latency_cachelines += cacheline_hw_access_latency;
    //     m_total_remote_dram_hardware_latency_cachelines_count++;
    // }
    SubsecondTime cacheline_hw_access_latency = getDramAccessCost(pkt_time, pkt_size, requester, address, perf, true, false, false);
    m_total_remote_dram_hardware_latency_cachelines += cacheline_hw_access_latency;
    m_total_remote_dram_hardware_latency_cachelines_count++;

    SubsecondTime t_now = pkt_time + cacheline_hw_access_latency;

    SubsecondTime t_remote_queue_request = t_now;  // time of making queue request (after DRAM hardware access cost added)
    // Compress
    UInt64 phys_page = address & ~((UInt64(1) << floorLog2(m_page_size)) - 1);
    UInt32 size = pkt_size;
    SubsecondTime cacheline_compression_latency = SubsecondTime::Zero();  // when cacheline compression is not enabled, this is always 0
    // if (m_use_compression)
    // {
    //     if (m_r_cacheline_gran) {
    //         if (m_r_partition_queues == 1) {
    //             m_compression_model->update_bandwidth_utilization(m_data_movement_2->getCachelineQueueUtilizationPercentage(t_now));
    //             m_compression_model->update_queue_model(m_data_movement_2, t_now, &m_r_part2_bandwidth, requester);
    //         }
    //         else if (m_r_partition_queues == 3 || m_r_partition_queues == 4) {
    //             m_compression_model->update_bandwidth_utilization(m_data_movement->getCachelineQueueUtilizationPercentage(t_now));
    //             m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_part2_bandwidth, requester);
    //         }
    //         else { // ie partition queues off
    //             m_compression_model->update_bandwidth_utilization(m_data_movement->getTotalQueueUtilizationPercentage(t_now));
    //             m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_bus_bandwidth, requester);
    //         }

    //         UInt32 compressed_cache_lines;
    //         cacheline_compression_latency = m_compression_model->compress(phys_page, m_cache_line_size, m_core_id, &size, &compressed_cache_lines);
    //         if (m_cache_line_size > size)
    //             bytes_saved += m_cache_line_size - size;
    //         else
    //             bytes_saved -= size - m_cache_line_size;

    //         address_to_compressed_size[phys_page] = size;
    //         address_to_num_cache_lines[phys_page] = compressed_cache_lines;
    //         m_total_compression_latency += cacheline_compression_latency;
    //         t_now += cacheline_compression_latency;
    //     } else if (m_use_cacheline_compression) {
    //         if (m_r_partition_queues == 1) {
    //             m_cacheline_compression_model->update_bandwidth_utilization(m_data_movement_2->getCachelineQueueUtilizationPercentage(t_now));
    //             m_cacheline_compression_model->update_queue_model(m_data_movement_2, t_now, &m_r_part2_bandwidth, requester);
    //         }
    //         else if (m_r_partition_queues == 3 || m_r_partition_queues == 4) {
    //             m_cacheline_compression_model->update_bandwidth_utilization(m_data_movement->getCachelineQueueUtilizationPercentage(t_now));
    //             m_cacheline_compression_model->update_queue_model(m_data_movement, t_now, &m_r_part2_bandwidth, requester);

    //         }
    //         else { // ie partition queues off
    //             m_cacheline_compression_model->update_bandwidth_utilization(m_data_movement->getTotalQueueUtilizationPercentage(t_now));
    //             m_cacheline_compression_model->update_queue_model(m_data_movement, t_now, &m_r_bus_bandwidth, requester);

    //         }

    //         UInt32 compressed_cache_lines;
    //         cacheline_compression_latency = m_cacheline_compression_model->compress(phys_page, m_cache_line_size, m_core_id, &size, &compressed_cache_lines);
    //         if (m_cache_line_size > size)
    //             cacheline_bytes_saved += m_cache_line_size - size;
    //         else
    //             cacheline_bytes_saved -= size - m_cache_line_size;
    //         address_to_num_cache_lines[phys_page] = compressed_cache_lines;
    //         m_total_cacheline_compression_latency += cacheline_compression_latency;
    //         t_now += cacheline_compression_latency;
    //     }
    // }
    // TODO: datamovement_delay is only added to t_now if(m_r_mode != 4 && !m_r_enable_selective_moves), should we also only add cacheline compression latency if the same condition is true?

    SubsecondTime cacheline_network_processing_time;
    if (m_r_partition_queues > 0)
        cacheline_network_processing_time = m_r_part2_bandwidth.getRoundedLatency(8*size);
    else
        cacheline_network_processing_time = m_r_bus_bandwidth.getRoundedLatency(8*size);
    SubsecondTime cacheline_queue_delay;
    if (m_r_partition_queues == 1) {
        // REMOTE_ONLY: Use computeQueueDelayTrackBytes here instead of computeQueueDelayNoEffect
        cacheline_queue_delay = m_data_movement_2->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    } else if (m_r_partition_queues == 2) {
        cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    } else if (m_r_partition_queues == 3) {
        cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    } else if (m_r_partition_queues == 4) {
        cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    } else {
        cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    }
    SubsecondTime datamovement_delay = (cacheline_queue_delay + cacheline_network_processing_time);
    m_cacheline_network_processing_time += cacheline_network_processing_time;
    m_cacheline_network_queue_delay += cacheline_queue_delay;
    m_remote_to_local_cacheline_move_count++;
    // All requests placed on the "cacheline" side of the queue
    if (m_r_partition_queues == 0 && m_data_movement->getCachelineQueueUtilizationPercentage(t_now) > m_r_page_queue_utilization_threshold) {
        // Extra movement would exceed network bandwidth
        SubsecondTime extra_delay_penalty = SubsecondTime::NS() * 10000;
        t_now += extra_delay_penalty;
        ++m_datamovement_queue_full_page_still_moved;
    }
    
    // TODO: Currently model decompression by adding decompression latency to inflight page time
    // if (m_use_compression && (m_r_cacheline_gran || m_use_cacheline_compression))
    // {
    //     CompressionModel *compression_model = m_r_cacheline_gran ? m_compression_model : m_cacheline_compression_model;
    //     SubsecondTime decompression_latency = compression_model->decompress(phys_page, address_to_num_cache_lines[phys_page], m_core_id);
    //     datamovement_delay += decompression_latency;
    //     if (m_r_cacheline_gran)
    //         m_total_decompression_latency += decompression_latency;
    //     else
    //         m_total_cacheline_decompression_latency += decompression_latency;
    // }

    // SubsecondTime ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * pkt_size);
    // t_now += ddr_processing_time;

    // Track access to page
    if (m_r_cacheline_gran)
        phys_page =  address & ~((UInt64(1) << floorLog2(m_cache_line_size)) - 1); // Was << 6
    bool move_page = true;
    // bool move_page = false;
    // if (m_r_mode == 2 || m_r_mode == 5) {  // Update m_remote_access_tracker
    //     auto it = m_remote_access_tracker.find(phys_page);
    //     if (it != m_remote_access_tracker.end()) {
    //         m_remote_access_tracker[phys_page] += 1; 
    //     }
    //     else {
    //         m_remote_access_tracker[phys_page] = 1;
    //     }
    //     m_recent_remote_accesses.insert(std::pair<SubsecondTime, UInt64>(t_now, phys_page));
    // }
    // if (m_r_mode == 2 && m_remote_access_tracker[phys_page] > m_r_datamov_threshold) {
    //     // Only move pages when the page has been accessed remotely m_r_datamov_threshold times
    //     move_page = true;
    // } 
    // if (m_r_mode == 1 || m_r_mode == 3) {
    //     move_page = true; 
    // }
    // if (m_r_mode == 5) {
    //     // Use m_recent_remote_accesses to update m_remote_access_tracker so that it only keeps recent (10^5 ns) remote accesses
    //     while (!m_recent_remote_accesses.empty() && m_recent_remote_accesses.begin()->first + m_r_mode_5_remote_access_history_window_size < t_now) {
    //         auto entry = m_recent_remote_accesses.begin();
    //         m_remote_access_tracker[entry->second] -= 1;
    //         m_recent_remote_accesses.erase(entry);
    //     }

    //     double page_queue_utilization = m_data_movement->getPageQueueUtilizationPercentage(t_now);
    //     if (page_queue_utilization < m_r_mode_5_limit_moves_threshold) {
    //         // If queue utilization percentage is less than threshold, always move.
    //         move_page = true;
    //     } else {
    //         if (m_remote_access_tracker[phys_page] > m_r_datamov_threshold) {
    //             // Otherwise, only move if the page has been accessed a threshold number of times.
    //             move_page = true;
    //             ++m_rmode5_page_moved_due_to_threshold;
    //         } else {
    //             ++m_move_page_cancelled_rmode5;  // we chose not to move the page
    //         }
    //     }
    // }
    // Cancel moving the page if the amount of reserved bufferspace in localdram for inflight + inflight_evicted pages is not enough to support an additional move
    // if (m_r_partition_queues != 0 && move_page && m_r_reserved_bufferspace > 0 && ((m_inflight_pages.size() + m_inflightevicted_pages.size())  >= ((double)m_r_reserved_bufferspace/100)*(m_localdram_size/m_page_size))) {
    //     move_page = false;
    //     ++m_move_page_cancelled_bufferspace_full;
    // }
    // // Cancel moving the page if the queue used to move the page is already full
    // if (m_r_partition_queues != 0 && move_page && m_data_movement->getPageQueueUtilizationPercentage(t_now) > m_r_page_queue_utilization_threshold) {  // save 5% for evicted pages?
    //     move_page = false;
    //     ++m_move_page_cancelled_datamovement_queue_full;

    //     if (m_use_throttled_pages_tracker) {
    //         // Track future accesses to throttled page
    //         auto it = m_throttled_pages_tracker.find(phys_page);
    //         if (it != m_throttled_pages_tracker.end() && t_now <= m_r_ideal_pagethrottle_remote_access_history_window_size + m_throttled_pages_tracker[phys_page].first) {
    //             // found it, and last throttle of this page was within 10^5 ns
    //             m_throttled_pages_tracker[phys_page].first = t_now;
    //             m_throttled_pages_tracker[phys_page].second += 1;
    //         } else {
    //             if (it != m_throttled_pages_tracker.end()) {  // there was previously a value in the map
    //                 // printf("disagg.cc: m_throttled_pages_tracker[%lu] max was %u\n", phys_page, m_throttled_pages_tracker[phys_page].second);
    //                 m_throttled_pages_tracker_values.push_back(std::pair<UInt64, UInt32>(phys_page, m_throttled_pages_tracker[phys_page].second));
    //             }
    //             m_throttled_pages_tracker[phys_page] = std::pair<SubsecondTime, UInt32>(t_now, 0);
    //         }
    //     }
    // } else if (m_use_throttled_pages_tracker) {
    //     // Page was not throttled
    //     auto it = m_throttled_pages_tracker.find(phys_page);
    //     if (it != m_throttled_pages_tracker.end()) {  // there was previously a value in the map
    //         // printf("disagg.cc: m_throttled_pages_tracker[%lu] max was %u\n", phys_page, m_throttled_pages_tracker[phys_page].second);
    //         m_throttled_pages_tracker_values.push_back(std::pair<UInt64, UInt32>(phys_page, m_throttled_pages_tracker[phys_page].second));
    //         m_throttled_pages_tracker.erase(phys_page);  // page was moved, clear
    //     }
    // }
   
    if (!m_r_use_separate_queue_model) {  // when a separate remote QueueModel is used, the network latency is added there
        t_now += m_r_added_latency;
    }
    if (m_r_mode != 4 && !m_r_enable_selective_moves) {
        t_now += datamovement_delay;
        m_total_remote_datamovement_latency += datamovement_delay;
    }

    perf->updateTime(t_now, ShmemPerf::DRAM_BUS);

    // SubsecondTime page_network_processing_time = SubsecondTime::Zero();

    // // Adding data movement cost of the entire page for now (this just adds contention in the queue)
    // SubsecondTime page_datamovement_delay = SubsecondTime::Zero();
    // if (move_page) {
    //     ++m_page_moves;
    //     SubsecondTime page_compression_latency = SubsecondTime::Zero();  // when page compression is not enabled, this is always 0
    //     SubsecondTime page_hw_access_latency = SubsecondTime::Zero();
    //     SubsecondTime local_page_hw_write_latency = SubsecondTime::Zero();
    //     std::vector<std::pair<UInt64, SubsecondTime>> updated_inflight_page_arrival_time_deltas;
    //     if (m_r_simulate_datamov_overhead && !m_r_cacheline_gran) {
    //         //check if queue is full
    //         //if it is... wait.
    //         //to wait: t_remote_queue_request + some amount of time
    //         //try again

    //         UInt32 page_size = m_page_size;
    //         if (m_r_partition_queues) {
    //             page_size = m_page_size - m_cache_line_size;
    //         }

    //         // Compress
    //         if (m_use_compression)
    //         {
    //             if (m_use_r_compressed_pages && address_to_compressed_size.find(phys_page) != address_to_compressed_size.end()) {
    //                 page_size = address_to_compressed_size[phys_page];
    //             } else {
    //                 m_compression_model->update_bandwidth_utilization(m_data_movement->getPageQueueUtilizationPercentage(t_now));
    //                 if (m_r_partition_queues == 1 || m_r_partition_queues == 3 || m_r_partition_queues == 4)
    //                     m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_part_bandwidth, requester);
    //                 else
    //                     m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_bus_bandwidth, requester);

    //                 UInt32 compressed_cache_lines;
    //                 size_t size_to_compress = (m_r_partition_queues) ? m_page_size - m_cache_line_size : m_page_size;
    //                 page_compression_latency = m_compression_model->compress(phys_page, size_to_compress, m_core_id, &page_size, &compressed_cache_lines);
    //                 if (m_page_size > page_size)
    //                     bytes_saved += m_page_size - page_size;
    //                 else
    //                     bytes_saved -= page_size - m_page_size;

    //                 address_to_compressed_size[phys_page] = page_size;
    //                 address_to_num_cache_lines[phys_page] = compressed_cache_lines;
    //                 m_total_compression_latency += page_compression_latency;
    //                 t_now += page_compression_latency;
    //             }
    //         }

    //         // Round page size for dram up to the nearest multiple of m_cache_line_size
    //         UInt32 page_size_for_dram = page_size;
    //         UInt32 cacheline_remainder = page_size % m_cache_line_size;
    //         if (cacheline_remainder > 0)
    //             page_size_for_dram = page_size_for_dram + m_cache_line_size - cacheline_remainder;
    //         if (m_r_partition_queues) {
    //             // Set exclude_cacheline to true (in this case, should still pass in the original page size for the size parameter)
    //             page_hw_access_latency = getDramAccessCost(t_remote_queue_request, page_size_for_dram, requester, address, perf, true, true, true);
    //             local_page_hw_write_latency = getDramWriteCost(t_remote_queue_request, page_size_for_dram, requester, address, perf, true, true);
    //         } else {
    //             page_hw_access_latency = getDramAccessCost(t_remote_queue_request, page_size_for_dram, requester, address, perf, true, false, true);
    //             local_page_hw_write_latency = getDramWriteCost(t_remote_queue_request, page_size_for_dram, requester, address, perf, false, true);
    //         }
    //         m_total_remote_dram_hardware_latency_pages += page_hw_access_latency;
    //         m_total_remote_dram_hardware_latency_pages_count++;
    //         m_total_local_dram_hardware_write_latency_pages += local_page_hw_write_latency;

    //         // Compute network transmission delay
    //         if (m_r_partition_queues > 0)
    //             page_network_processing_time = m_r_part_bandwidth.getRoundedLatency(8*page_size);  // default of moving the whole page, use processing time of just the cacheline if we use that in the critical path
    //         else
    //             page_network_processing_time = m_r_bus_bandwidth.getRoundedLatency(8*page_size);

    //         if (m_r_partition_queues == 1) {
    //             page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_hw_access_latency + page_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*page_size), page_size, QueueModel::PAGE, requester);
    //         } else if (m_r_partition_queues == 2) {
    //             page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_hw_access_latency + page_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*page_size), page_size, QueueModel::PAGE, requester);
    //         } else if (m_r_partition_queues == 3) {
    //             page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_hw_access_latency + page_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*page_size), page_size, QueueModel::PAGE, requester, true, phys_page);
    //         } else if (m_r_partition_queues == 4) {
    //             page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_hw_access_latency + page_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*page_size), page_size, QueueModel::PAGE, requester);
    //         } else {
    //             page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_hw_access_latency + page_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*page_size), page_size, QueueModel::PAGE, requester);
    //         }

    //         // TODO: Currently model decompression by adding decompression latency to inflight page time
    //         SubsecondTime page_decompression_latency = SubsecondTime::Zero();
    //         if (m_use_compression)
    //         {
    //             page_decompression_latency = m_compression_model->decompress(phys_page, address_to_num_cache_lines[phys_page], m_core_id);
    //             page_datamovement_delay += page_decompression_latency;
    //             m_total_decompression_latency += page_decompression_latency;
    //         }

    //         // Update t_now after page_datamovement_delay includes the decompression latency
    //         if (m_r_partition_queues != 0) {
    //             if (page_hw_access_latency + page_compression_latency + page_datamovement_delay <= cacheline_hw_access_latency + cacheline_compression_latency + datamovement_delay) {
    //                 // If the page arrival time via the page queue is faster than the cacheline via the cacheline queue, use the page queue arrival time
    //                 // (and the cacheline request is not sent)
    //                 t_now += (page_datamovement_delay + page_network_processing_time);  // if nonzero, compression latency was earlier added to t_now already
    //                 m_total_remote_datamovement_latency += (page_datamovement_delay + page_network_processing_time);
    //                 m_page_network_processing_time += page_network_processing_time;
    //                 m_page_network_queue_delay += page_datamovement_delay - page_decompression_latency;
    //                 m_remote_to_local_page_move_count++;
    //                 // m_total_remote_dram_hardware_latency_cachelines -= cacheline_hw_access_latency;  // remove previously added latency
    //                 t_now -= cacheline_hw_access_latency;  // remove previously added latency
    //                 t_now += page_hw_access_latency;
    //                 t_now += local_page_hw_write_latency;
    //                 if (m_r_mode != 4 && !m_r_enable_selective_moves) {
    //                     t_now -= datamovement_delay;  // only subtract if it was added earlier
    //                     m_total_remote_datamovement_latency -= datamovement_delay;
    //                     m_cacheline_network_processing_time -= cacheline_network_processing_time;
    //                     m_cacheline_network_queue_delay -= cacheline_queue_delay;
    //                     m_remote_to_local_cacheline_move_count--;
    //                 }
    //                 if (m_use_compression && (m_r_cacheline_gran || m_use_cacheline_compression))
    //                     t_now -= cacheline_compression_latency;  // essentially didn't separately compress cacheline, so subtract this previously added compression latency
    //                 ++partition_queues_cacheline_slower_than_page;
    //             } else {
    //                 // Actually put the cacheline request on the cacheline queue, since after checking the page arrival time we're sure we actually use the cacheline request
    //                 // This is an ideal situation, since in real life we might not be able to compare these times, not actually send the cacheline request, avoid the cacheline compression latency, etc
    //                 if (m_r_partition_queues == 1) {
    //                     cacheline_queue_delay = m_data_movement_2->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                 } else if (m_r_partition_queues == 2) {
    //                     cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                 } else if (m_r_partition_queues == 3) {
    //                     // Delayed inflight pages, if any, updated after m_inflight_pages includes the current page
    //                     cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytesPotentialPushback(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, updated_inflight_page_arrival_time_deltas, true, requester);
    //                 } else if (m_r_partition_queues == 4) {
    //                     cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                 }
    //                 t_now -= page_compression_latency;  // Page compression is not on critical path; this is 0 if compression is off
    //                 ++m_redundant_moves;
    //                 ++m_redundant_moves_type1;
    //                 m_redundant_moves_type1_time_savings += (page_hw_access_latency + page_compression_latency + page_datamovement_delay) - (cacheline_hw_access_latency + cacheline_compression_latency + datamovement_delay);
    //                 // LOG_PRINT("partition_queue=1 resulted in savings of APPROX %lu ns in getAccessLatencyRemote", ((page_compression_latency + page_datamovement_delay) - (cacheline_compression_latency + datamovement_delay)).getNS());
    //             }
    //         } else {
    //             // Default is requesting the whole page at once (instead of also requesting cacheline), so replace time of cacheline request with the time of the page request
    //             t_now += (page_datamovement_delay + page_network_processing_time);
    //             m_total_remote_datamovement_latency += (page_datamovement_delay + page_network_processing_time);
    //             m_page_network_processing_time += page_network_processing_time;
    //             m_page_network_queue_delay += page_datamovement_delay - page_decompression_latency;
    //             m_remote_to_local_page_move_count++;
    //             t_now += page_hw_access_latency;
    //             t_now += local_page_hw_write_latency;
    //             if (m_r_mode != 4 && !m_r_enable_selective_moves) {
    //                 t_now -= datamovement_delay;  // only subtract if it was added earlier
    //                 m_total_remote_datamovement_latency -= datamovement_delay;
    //                 m_cacheline_network_processing_time -= cacheline_network_processing_time;
    //                 m_cacheline_network_queue_delay -= cacheline_queue_delay;
    //                 m_remote_to_local_cacheline_move_count--;
    //             }
    //             if (m_use_compression && (m_r_cacheline_gran || m_use_cacheline_compression))
    //                 t_now -= cacheline_compression_latency;  // essentially didn't separately compress cacheline, so subtract this previously added compression latency
    //         }
    //     } else if (!m_r_simulate_datamov_overhead) {
    //         // Include the hardware access cost of the page
    //         page_hw_access_latency = getDramAccessCost(t_remote_queue_request, m_page_size, requester, address, perf, true, false, true);
    //         t_now += page_hw_access_latency;
    //         t_now += local_page_hw_write_latency;
    //         m_total_remote_dram_hardware_latency_pages += page_hw_access_latency;
    //         m_total_remote_dram_hardware_latency_pages_count++;
    //         m_total_local_dram_hardware_write_latency_pages += local_page_hw_write_latency;
    //         if (m_r_mode != 4 && !m_r_enable_selective_moves) {
    //             t_now -= datamovement_delay;  // only subtract if it was added earlier
    //             m_total_remote_datamovement_latency -= datamovement_delay;
    //             m_cacheline_network_processing_time -= cacheline_network_processing_time;
    //             m_cacheline_network_queue_delay -= cacheline_queue_delay;
    //             m_remote_to_local_cacheline_move_count--;
    //         }
    //     } else {
    //         page_datamovement_delay = SubsecondTime::Zero();
    //     }


    //     // assert(std::find(m_local_pages.begin(), m_local_pages.end(), phys_page) == m_local_pages.end()); 
    //     // assert(std::find(m_remote_pages.begin(), m_remote_pages.end(), phys_page) != m_remote_pages.end());
    //     // Commenting these out since for this function to be called by isRemoteAccess() returning true, these statements are already true
    //     // assert(!m_local_pages.find(phys_page));
    //     // assert(m_remote_pages.count(phys_page));
    //     m_local_pages.push_back(phys_page);
    //     if (m_r_exclusive_cache)
    //         m_remote_pages.erase(phys_page);
    //     // if (!m_speed_up_simulation)
    //     //     m_local_pages_remote_origin[phys_page] = 1;
    //     if (m_use_ideal_page_throttling || !m_speed_up_simulation)
    //         m_moved_pages_no_access_yet.push_back(phys_page);


    //     SubsecondTime global_time = Sim()->getClockSkewMinimizationServer()->getGlobalTime();
    //     SubsecondTime page_arrival_time = t_remote_queue_request + page_hw_access_latency + page_compression_latency + page_datamovement_delay + page_network_processing_time + local_page_hw_write_latency;  // page_datamovement_delay already contains the page decompression latency
    //     if (global_time > page_arrival_time + SubsecondTime::NS(50)) {  // if global time is more than 50 ns ahead of page_arrival_time
    //         ++m_global_time_much_larger_than_page_arrival;
    //         m_sum_global_time_much_larger += global_time - page_arrival_time;
    //     }
    //     if (m_r_partition_queues == 0 && move_page && m_r_reserved_bufferspace > 0 && ((m_inflight_pages.size() + m_inflightevicted_pages.size()) >= ((double)m_r_reserved_bufferspace/100)*(m_localdram_size/m_page_size))) {
    //         // PQ=off, page movement would exceed inflight pages bufferspace
    //         // Estimate how long it would take to send packet if inflight page buffer is full
    //         SubsecondTime extra_delay_penalty = SubsecondTime::NS() * 2000;
    //         t_now += extra_delay_penalty;
    //         m_inflight_pages_extra[phys_page] = SubsecondTime::max(global_time, page_arrival_time + extra_delay_penalty);
    //         ++m_bufferspace_full_page_still_moved;
    //         if (m_inflight_pages_extra.size() > m_max_inflight_extra_bufferspace)
    //             m_max_inflight_extra_bufferspace++;
    //     } else if (m_r_partition_queues == 0 && move_page && m_data_movement->getPageQueueUtilizationPercentage(t_now) > m_r_page_queue_utilization_threshold) {
    //         // PQ=off, page movement would exceed network bandwidth
    //         // Estimate how long it would take to send packet if inflight page buffer is full
    //         SubsecondTime extra_delay_penalty = SubsecondTime::NS() * 10000;
    //         t_now += extra_delay_penalty;
    //         // m_inflight_pages_extra[phys_page] = SubsecondTime::max(global_time, page_arrival_time + extra_delay_penalty);
    //         ++m_datamovement_queue_full_page_still_moved;
    //         // if (m_inflight_pages_extra.size() > m_max_inflight_extra_bufferspace)
    //         //     m_max_inflight_extra_bufferspace++;
    //     } else {
    //         // m_inflight_pages.erase(phys_page);
    //         m_inflight_pages[phys_page] = SubsecondTime::max(global_time, page_arrival_time);
    //         m_inflight_redundant[phys_page] = 0; 
    //         if (m_inflight_pages.size() > m_max_inflight_bufferspace)
    //             m_max_inflight_bufferspace++;
    //         if (m_inflight_pages.size() + m_inflightevicted_pages.size() > m_max_total_bufferspace)
    //             m_max_total_bufferspace++;  // update stat

    //         for (auto it = updated_inflight_page_arrival_time_deltas.begin(); it != updated_inflight_page_arrival_time_deltas.end(); ++it) {
    //             if (m_inflight_pages.count(it->first) && it->second > SubsecondTime::Zero()) {
    //                 m_inflight_pages_delay_time += it->second;
    //                 m_inflight_pages[it->first] += it->second;  // update arrival time if it's still an inflight page
    //                 ++m_inflight_page_delayed;
    //             }
    //         }
    //     }

    // }
    // if (!move_page || !m_r_simulate_datamov_overhead || m_r_cacheline_gran) {  // move_page == false, or earlier condition (m_r_simulate_datamov_overhead && !m_r_cacheline_gran) is false
    //     // Actually put the cacheline request on the queue, since after now we're sure we actually use the cacheline request
    //     // This actual cacheline request probably has a similar delay value as the earlier computeQueueDelayNoEffect value, no need to update t_now
    //     // When partition queues is on, the cacheline HW access cost request was already made at the beginning of this function
    //     if (!m_r_partition_queues) {
    //         // When partition queues is off, only calculate cacheline access cost when moving the cacheline instead of the page
    //         cacheline_hw_access_latency = getDramAccessCost(pkt_time, pkt_size, requester, address, perf, true, false, false);  // A change for 64 byte page granularity
    //         t_remote_queue_request = pkt_time + cacheline_hw_access_latency;
    //         m_total_remote_dram_hardware_latency_cachelines += cacheline_hw_access_latency;
    //         m_total_remote_dram_hardware_latency_cachelines_count++;
    //         t_now += cacheline_hw_access_latency;
    //     }

    //     if (m_r_partition_queues == 1) {
    //         cacheline_queue_delay = m_data_movement_2->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //     } else if (m_r_partition_queues == 2) {
    //         cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //     } else if (m_r_partition_queues == 3) {
    //         std::vector<std::pair<UInt64, SubsecondTime>> updated_inflight_page_arrival_time_deltas;
    //         cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytesPotentialPushback(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, updated_inflight_page_arrival_time_deltas, true, requester);
    //         for (auto it = updated_inflight_page_arrival_time_deltas.begin(); it != updated_inflight_page_arrival_time_deltas.end(); ++it) {
    //             if (m_inflight_pages.count(it->first) && it->second > SubsecondTime::Zero()) {
    //                 m_inflight_pages_delay_time += it->second;
    //                 m_inflight_pages[it->first] += it->second;  // update arrival time if it's still an inflight page
    //                 ++m_inflight_page_delayed;
    //             }
    //         }
    //     } else if (m_r_partition_queues == 4) {
    //         cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //     } else {
    //         // partition queues off, but move_page = false so actually put the cacheline request on the queue
    //         cacheline_queue_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + cacheline_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //     }
    // } 

    // if (move_page) { // Check if there's place in local DRAM and if not evict an older page to make space
    //     t_now += possiblyEvict(phys_page, pkt_time, requester);
    // }

    // Update Memory Counters?
    //queue_delay = ddr_queue_delay;

    //std::cout << "Remote Latency: " << t_now - pkt_time << std::endl;
    possiblyPrefetch(phys_page, t_now, requester);

    SubsecondTime access_latency = t_now - pkt_time;
    m_total_remote_access_latency += access_latency;
    m_total_access_latency += access_latency;
    update_local_remote_latency_stat(access_latency);
    return access_latency;
}

void
DramPerfModelDisaggRemoteOnly::updateLocalIPCStat()
{
    if (IPC_window_cur_size == 0)
        IPC_window_start_instr_count = Sim()->getCoreManager()->getCoreFromID(0)->getInstructionCount();
    IPC_window_cur_size += 1;
    if (IPC_window_cur_size == IPC_window_capacity) {
        ComponentPeriod cp = ComponentPeriod::fromFreqHz(1000000000 * Sim()->getCfg()->getFloat("perf_model/core/frequency"));
        SubsecondTimeCycleConverter converter = SubsecondTimeCycleConverter(&cp);

        IPC_window_end_instr_count = Sim()->getCoreManager()->getCoreFromID(0)->getInstructionCount();
        UInt64 instructions = IPC_window_end_instr_count - IPC_window_start_instr_count;
        UInt64 cycles = converter.subsecondTimeToCycles(SubsecondTime::NS(1000) * IPC_window_capacity);
        double IPC = instructions / (double) cycles;
        std::round(IPC * 100000.0) / 100000.0;
        m_local_IPCs.push_back(IPC);

        IPC_window_cur_size = 0;
    }
}

void
DramPerfModelDisaggRemoteOnly::update_local_remote_latency_stat(SubsecondTime access_latency)
{
    m_local_total_remote_access_latency += access_latency;
    m_local_total_remote_access_latency_window_cur_size += 1;
    if (m_local_total_remote_access_latency_window_cur_size == m_local_total_remote_access_latency_window_capacity) {
        m_local_total_remote_access_latency_avgs.push_back(m_local_total_remote_access_latency / (float) m_local_total_remote_access_latency_window_capacity);
        m_local_total_remote_access_latency = SubsecondTime::Zero();
        m_local_total_remote_access_latency_window_cur_size = 0;
    }
}

void
DramPerfModelDisaggRemoteOnly::updateBandwidth()
{
    m_update_bandwidth_count += 1;
    if (m_use_dynamic_bandwidth && m_update_bandwidth_count % 20 == 0) {
        m_r_bw_scalefactor = (int)(m_r_bw_scalefactor + 1) % 17;
        if (m_r_bw_scalefactor == 0)
            m_r_bw_scalefactor = 4;

        m_r_bus_bandwidth.changeBandwidth(m_dram_speed * m_data_bus_width / (1000 * m_r_bw_scalefactor)); // Remote memory
        m_r_part_bandwidth.changeBandwidth(m_dram_speed * m_data_bus_width / (1000 * m_r_bw_scalefactor / (1 - m_r_cacheline_queue_fraction))); // Remote memory - Partitioned Queues => Page Queue
        m_r_part2_bandwidth.changeBandwidth(m_dram_speed * m_data_bus_width / (1000 * m_r_bw_scalefactor / m_r_cacheline_queue_fraction)); // Remote memory - Partitioned Queues => Cacheline Queue
        // Currently only windowed_mg1_remote_ind_queues QueueModel updates stats tracking based on updateBandwidth()
        m_data_movement->updateBandwidth(m_r_bus_bandwidth.getBandwidthBitsPerUs(), m_r_cacheline_queue_fraction);
    } else if (m_use_dynamic_cl_queue_fraction_adjustment) {
        // Same formulas here as in DramPerfModelDisaggRemoteOnly constructor
        m_r_bus_bandwidth.changeBandwidth(m_dram_speed * m_data_bus_width / (1000 * m_r_bw_scalefactor)); // Remote memory
        m_r_part_bandwidth.changeBandwidth(m_dram_speed * m_data_bus_width / (1000 * m_r_bw_scalefactor / (1 - m_r_cacheline_queue_fraction))); // Remote memory - Partitioned Queues => Page Queue
        m_r_part2_bandwidth.changeBandwidth(m_dram_speed * m_data_bus_width / (1000 * m_r_bw_scalefactor / m_r_cacheline_queue_fraction)); // Remote memory - Partitioned Queues => Cacheline Queue
        // Currently only windowed_mg1_remote_ind_queues QueueModel updates stats tracking based on updateBandwidth()
        m_data_movement->updateBandwidth(m_r_bus_bandwidth.getBandwidthBitsPerUs(), m_r_cacheline_queue_fraction);
    }
}

void
DramPerfModelDisaggRemoteOnly::updateLatency()
{
    m_update_latency_count += 1;
    if (m_use_dynamic_latency && m_update_latency_count % 20 == 0) {
        m_r_added_latency_int = (m_r_added_latency_int + 100) % 1700;
        if (m_r_added_latency_int == 0)
            m_r_added_latency_int = 400;
        m_r_added_latency.setInternalDataForced(1000000UL * m_r_added_latency_int);
    }
}

void
DramPerfModelDisaggRemoteOnly::update_bw_utilization_count(SubsecondTime pkt_time)
{
    double bw_utilization = m_data_movement->getPageQueueUtilizationPercentage(pkt_time);
    int decile = (int)(bw_utilization * 10);
    if (decile > 10)
        decile = 10;  // put all utilizations above 100% here
    else if (decile < 0)
        std::cout << "update_bw_utilization_count() decile < 0, returned page bw utilization is wrong?" << std::endl;
    m_bw_utilization_decile_to_count[decile] += 1;
}

SubsecondTime
DramPerfModelDisaggRemoteOnly::getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf)
{
    // Update bandwidth factor every 1K remote accesses
    /*
    // if (m_use_dynamic_bandwidth && m_num_accesses + 1 % 1000 == 0)
    //     updateBandwidth();
    // */

    if (m_track_page_bw_utilization_stats) {
        // Update BW utilization count
        update_bw_utilization_count(pkt_time);
    }
    // Update BW utilization stat
    // 5 decimal precision
    if (m_r_partition_queues) {
        m_cacheline_bw_utilization_sum += m_data_movement->getCachelineQueueUtilizationPercentage(pkt_time) * m_r_cacheline_queue_fraction * 100000;
        m_page_bw_utilization_sum += m_data_movement->getPageQueueUtilizationPercentage(pkt_time) * (1. - m_r_cacheline_queue_fraction) * 100000;
        m_total_bw_utilization_sum += m_data_movement->getCachelineQueueUtilizationPercentage(pkt_time) * m_r_cacheline_queue_fraction + m_data_movement->getPageQueueUtilizationPercentage(pkt_time) * (1. - m_r_cacheline_queue_fraction) * 100000;
    } else {
        m_total_bw_utilization_sum += m_data_movement->getPageQueueUtilizationPercentage(pkt_time) * 100000;
    }

    UInt64 phys_page = address & ~((UInt64(1) << floorLog2(m_page_size)) - 1);
    if (m_r_cacheline_gran)
        phys_page =  address & ~((UInt64(1) << floorLog2(m_cache_line_size)) - 1); // Was << 6
    UInt64 cacheline =  address & ~((UInt64(1) << floorLog2(m_cache_line_size)) - 1); // Was << 6

    // if (!m_speed_up_simulation) {
    //     if (m_page_usage_map.count(phys_page) == 0) {
    //         ++m_unique_pages_accessed;
    //         m_page_usage_map[phys_page] = 0;
    //     } else {
    //         m_page_usage_map[phys_page] += 1;
    //     }
    // }

    // SubsecondTime current_time = SubsecondTime::max(Sim()->getClockSkewMinimizationServer()->getGlobalTime(), pkt_time);
    // // m_inflight_pages: tracks which pages are being moved and when the movement will complete
    // // Check if the page movement is over and if so, remove from the list
    // // Do this for both queues, forward and backward
    // std::map<UInt64, SubsecondTime>::iterator i;
    // for (i = m_inflight_pages.begin(); i != m_inflight_pages.end();) {
    //     if (i->second <= current_time) {
    //         m_inflight_redundant.erase(i->first);
    //         m_data_movement->removeInflightPage(i->first);
    //         m_inflight_pages.erase(i++);

    //         // Dirty write buffer
    //         UInt64 inflight_page = i->first;
    //         if (m_inflight_page_to_dirty_write_count.find(inflight_page) != m_inflight_page_to_dirty_write_count.end()) {
    //             m_dirty_write_buffer_size -= m_inflight_page_to_dirty_write_count[inflight_page];
    //             m_inflight_page_to_dirty_write_count.erase(inflight_page);
    //             m_max_dirty_write_buffer_size = std::max(m_max_dirty_write_buffer_size, m_dirty_write_buffer_size);
    //         }
    //     } else {
    //         ++i;
    //     }
    // }
    // // Update dirty write buffer avg stat
    // m_sum_write_buffer_size += m_dirty_write_buffer_size;
    
    // for (i = m_inflight_pages_extra.begin(); i != m_inflight_pages_extra.end();) {
    //     if (i->second <= current_time) {
    //         m_inflight_pages_extra.erase(i++);
    //     } else {
    //         ++i;
    //     }
    // }

    // for (i = m_inflightevicted_pages.begin(); i != m_inflightevicted_pages.end();) {
    //     if (i->second <= current_time) {
    //         m_inflightevicted_pages.erase(i++);
    //     } else {
    //         ++i;
    //     }
    // }

    // Other systems using the remote memory and creating disturbance
    if (m_r_disturbance_factor > 0) {
        if ( (unsigned int)(rand() % 100) < m_r_disturbance_factor) {
            SubsecondTime delay(SubsecondTime::NS() * 1000);
            if (m_r_partition_queues == 1)  
                /* SubsecondTime page_datamovement_delay = */ m_data_movement->computeQueueDelayTrackBytes(pkt_time + delay, m_r_part_bandwidth.getRoundedLatency(8*m_page_size), m_page_size, QueueModel::PAGE, requester);
            else if (m_r_partition_queues == 2)
                /* SubsecondTime page_datamovement_delay = */ m_data_movement->computeQueueDelayTrackBytes(pkt_time + delay, m_r_bus_bandwidth.getRoundedLatency(8*m_page_size), m_page_size, QueueModel::PAGE, requester);
            else if (m_r_partition_queues == 3)
                /* SubsecondTime page_datamovement_delay = */ m_data_movement->computeQueueDelayTrackBytes(pkt_time + delay, m_r_part_bandwidth.getRoundedLatency(8*m_page_size), m_page_size, QueueModel::PAGE, requester);
            else if (m_r_partition_queues == 4)
                /* SubsecondTime page_datamovement_delay = */ m_data_movement->computeQueueDelayTrackBytes(pkt_time + delay, m_r_part_bandwidth.getRoundedLatency(8*m_page_size), m_page_size, QueueModel::PAGE, requester);
            else	
                /* SubsecondTime page_datamovement_delay = */ m_data_movement->computeQueueDelayTrackBytes(pkt_time + delay, m_r_bus_bandwidth.getRoundedLatency(8*m_page_size), m_page_size, QueueModel::PAGE, requester);
            m_extra_pages++;	
        } 
    }

    // // Every 1000 cacheline requests, update page locality stats and determine whether to adjust cacheline queue ratio
    // ++m_num_accesses;
    // if (m_num_accesses % 30000 == 0) {
    //     LOG_PRINT("Processed %lu cacheline requests", m_num_accesses);
    //     if (m_use_dynamic_cl_queue_fraction_adjustment || !m_speed_up_simulation) {  // only track if using dynamic cl queue fraction, or if not speeding up simulation
    //         UInt64 total_recent_accesses = m_num_recent_remote_accesses + m_num_recent_remote_additional_accesses + m_num_recent_local_accesses;
    //         UInt64 total_recent_pages = m_recent_accessed_pages.size();
    //         // average number of cachelines accessed per unique page
    //         double true_page_locality_measure = (double)total_recent_accesses / total_recent_pages;
    //         m_page_locality_measures.push_back(true_page_locality_measure);
            
    //         // counts every non-first access to a page as a "local" access
    //         double modified_page_locality_measure = (double)(m_num_recent_local_accesses + m_num_recent_remote_additional_accesses) / total_recent_accesses;
    //         // counts every non-first access to a page that wasn't accessed again via the cacheline queue as a "local" access
    //         double modified2_page_locality_measure = (double)m_num_recent_local_accesses / total_recent_accesses;
    //         m_modified_page_locality_measures.push_back(modified_page_locality_measure);
    //         m_modified2_page_locality_measures.push_back(modified2_page_locality_measure);

    //         // Adjust cl queue fraction if needed
    //         double cacheline_queue_utilization_percentage;
    //         if (m_r_partition_queues == 1)
    //             cacheline_queue_utilization_percentage = m_data_movement_2->getCachelineQueueUtilizationPercentage(pkt_time);
    //         else
    //             cacheline_queue_utilization_percentage = m_data_movement->getCachelineQueueUtilizationPercentage(pkt_time); 
    //         if (m_use_dynamic_cl_queue_fraction_adjustment && m_r_partition_queues != 0 && (m_data_movement->getPageQueueUtilizationPercentage(pkt_time) > 0.8 || cacheline_queue_utilization_percentage > 0.8)) {
    //             // Consider adjusting m_r_cacheline_queue_fraction
    //             // 0.2 is chosen as the "baseline" cacheline queue fraction, 0.025 is chosen as a step size
    //             if (true_page_locality_measure > 40 + std::max(0.0, (0.2 - m_r_cacheline_queue_fraction) * 100)) {
    //                 // Page locality high, increase prioritization of cachelines
    //                 m_r_cacheline_queue_fraction -= 0.025;
    //                 if (m_r_cacheline_queue_fraction < 0.1)  // min cl queue fraction
    //                     m_r_cacheline_queue_fraction = 0.1;
    //                 else
    //                     ++m_r_cacheline_queue_fraction_decreased;
    //             } else if (true_page_locality_measure < 20 - std::max(0.0, (m_r_cacheline_queue_fraction - 0.2) * 50)) {
    //                 // Page locality low, increase prioritization of cachelines
    //                 m_r_cacheline_queue_fraction += 0.025;
    //                 if (m_r_cacheline_queue_fraction > 0.6)  // max cl queue fraction (for now)
    //                     m_r_cacheline_queue_fraction = 0.6;
    //                 else
    //                     ++m_r_cacheline_queue_fraction_increased;
    //             }
    //             updateBandwidth();

    //             // Update stats
    //             if (m_r_cacheline_queue_fraction < m_min_r_cacheline_queue_fraction) {
    //                 m_min_r_cacheline_queue_fraction = m_r_cacheline_queue_fraction;
    //                 m_min_r_cacheline_queue_fraction_stat_scaled = m_min_r_cacheline_queue_fraction * 10000;
    //             }
    //             if (m_r_cacheline_queue_fraction > m_max_r_cacheline_queue_fraction) {
    //                 m_max_r_cacheline_queue_fraction = m_r_cacheline_queue_fraction;
    //                 m_max_r_cacheline_queue_fraction_stat_scaled = m_max_r_cacheline_queue_fraction * 10000;
    //             }
    //         }

    //         // Reset variables
    //         m_num_recent_remote_accesses = 0;
    //         m_num_recent_remote_additional_accesses = 0;   // For cacheline queue requests made on inflight pages. Track this separately since they could be counted as either "remote" or "local" cacheline accesses
    //         m_num_recent_local_accesses = 0;
    //         m_recent_accessed_pages.clear();
    //         // m_recent_access_count_begin_time = Sim()->getClockSkewMinimizationServer()->getGlobalTime();
    //     }
    // }
    // if (m_use_dynamic_cl_queue_fraction_adjustment || !m_speed_up_simulation) {  // only track if using dynamic cl queue fraction, or if not speeding up simulation
    //     if (!m_recent_accessed_pages.count(phys_page)) {
    //         m_recent_accessed_pages.insert(phys_page);
    //     }
    // }

    // Should we enable a remote access?
    // if (m_enable_remote_mem && isRemoteAccess(address, requester, access_type)) {
    if (access_type == DramCntlrInterface::READ) {
        ++m_remote_reads;
    } else {  // access_type == DramCntlrInterface::WRITE
        ++m_remote_writes;
    }
    // ++m_num_recent_remote_accesses;
    //	printf("Remote access: %d\n",m_remote_reads); 
    return getAccessLatencyRemote(pkt_time, pkt_size, requester, address, access_type, perf); 
    // }
    // if (!m_speed_up_simulation) {
    //     // m_local_reads_remote_origin
    //     if (m_local_pages_remote_origin.count(phys_page)) {
    //         m_local_pages_remote_origin[phys_page] += 1;
    //         if (access_type == DramCntlrInterface::READ) {
    //             ++m_local_reads_remote_origin;
    //         } else {  // access_type == DramCntlrInterface::WRITE
    //             ++m_local_writes_remote_origin;
    //         }
    //     }
    // }
    // if (m_use_ideal_page_throttling || !m_speed_up_simulation)
    //     m_moved_pages_no_access_yet.remove(phys_page);  // there has been a local access to phys_page

    // pkt_size is in 'Bytes'
    // m_dram_bandwidth is in 'Bits per clock cycle'

    // // Calculate address mapping inside the DIMM
    // UInt32 channel, rank, bank_group, bank, column;
    // UInt64 dram_page;
    // parseDeviceAddress(address, channel, rank, bank_group, bank, column, dram_page);


    // SubsecondTime t_now = pkt_time;
    // perf->updateTime(t_now);

    // // Add DDR controller pipeline delay
    // t_now += m_controller_delay;
    // perf->updateTime(t_now, ShmemPerf::DRAM_CNTLR);

    // // Add DDR refresh delay if needed
    // if (m_refresh_interval != SubsecondTime::Zero()) {
    //     SubsecondTime refresh_base = (t_now.getPS() / m_refresh_interval.getPS()) * m_refresh_interval;
    //     if (t_now - refresh_base < m_refresh_length) {
    //         t_now = refresh_base + m_refresh_length;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_REFRESH);
    //     }
    // }


    // UInt64 crb = (channel * m_num_ranks * m_num_banks) + (rank * m_num_banks) + bank; // Combine channel, rank, bank to index m_banks
    // LOG_ASSERT_ERROR(crb < m_total_banks, "Bank index out of bounds");
    // BankInfo &bank_info = m_banks[crb];

    // //printf("[%2d] %s (%12lx, %4lu, %4lu), t_open = %lu, t_now = %lu, bank_info.t_avail = %lu\n", m_core_id, bank_info.open_page == dram_page && bank_info.t_avail + m_bank_keep_open >= t_now ? "Page Hit: " : "Page Miss:", address, crb, dram_page % 10000, t_now.getNS() - bank_info.t_avail.getNS(), t_now.getNS(), bank_info.t_avail.getNS());

    // // DRAM page hit/miss
    // if (bank_info.open_page == dram_page                       // Last access was to this row
    //         && bank_info.t_avail + m_bank_keep_open >= t_now   // Bank hasn't been closed in the meantime
    //    ) {
    //     if (bank_info.t_avail > t_now) {
    //         t_now = bank_info.t_avail;
    //         perf->updateTime(t_now, ShmemPerf::DRAM_BANK_PENDING);
    //     }
    //     ++m_dram_page_hits;

    // } else {
    //     // Wait for bank to become available
    //     if (bank_info.t_avail > t_now)
    //         t_now = bank_info.t_avail;
    //     // Close dram_page
    //     if (bank_info.t_avail + m_bank_keep_open >= t_now) {
    //         // We found the dram_page open and have to close it ourselves
    //         t_now += m_bank_close_delay;
    //         ++m_dram_page_misses;
    //     } else if (bank_info.t_avail + m_bank_keep_open + m_bank_close_delay > t_now) {
    //         // Bank was being closed, we have to wait for that to complete
    //         t_now = bank_info.t_avail + m_bank_keep_open + m_bank_close_delay;
    //         ++m_dram_page_closing;
    //     } else {
    //         // Bank was already closed, no delay.
    //         ++m_dram_page_empty;
    //     }

    //     // Open dram_page
    //     t_now += m_bank_open_delay;
    //     perf->updateTime(t_now, ShmemPerf::DRAM_BANK_CONFLICT);

    //     bank_info.open_page = dram_page;
    // }

    // // Add rank access time and availability
    // UInt64 cr = (channel * m_num_ranks) + rank;
    // LOG_ASSERT_ERROR(cr < m_total_ranks, "Rank index out of bounds");
    // SubsecondTime rank_avail_request = (m_num_bank_groups > 1) ? m_intercommand_delay_short : m_intercommand_delay;
    // SubsecondTime rank_avail_delay = m_rank_avail.size() ? m_rank_avail[cr]->computeQueueDelay(t_now, rank_avail_request, requester) : SubsecondTime::Zero();

    // // Add bank group access time and availability
    // UInt64 crbg = (channel * m_num_ranks * m_num_bank_groups) + (rank * m_num_bank_groups) + bank_group;
    // LOG_ASSERT_ERROR(crbg < m_total_bank_groups, "Bank-group index out of bounds");
    // SubsecondTime group_avail_delay = m_bank_group_avail.size() ? m_bank_group_avail[crbg]->computeQueueDelay(t_now, m_intercommand_delay_long, requester) : SubsecondTime::Zero();

    // // Add device access time (tCAS)
    // t_now += m_dram_access_cost;
    // perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    // // Mark bank as busy until it can receive its next command
    // // Done before waiting for the bus to be free: sort of assumes best-case bus scheduling
    // bank_info.t_avail = t_now;

    // // Add the wait time for the larger of bank group and rank availability delay
    // t_now += (rank_avail_delay > group_avail_delay) ? rank_avail_delay : group_avail_delay;
    // perf->updateTime(t_now, ShmemPerf::DRAM_DEVICE);

    // // DDR bus latency and queuing delay
    // SubsecondTime ddr_processing_time = m_bus_bandwidth.getRoundedLatency(8 * pkt_size); // bytes to bits
    // //std::cout << m_bus_bandwidth.getRoundedLatency(8*pkt_size) << std::endl;  
    // SubsecondTime ddr_queue_delay = m_queue_model.size() ? m_queue_model[channel]->computeQueueDelay(t_now, ddr_processing_time, requester) : SubsecondTime::Zero();
    // t_now += ddr_queue_delay;
    // perf->updateTime(t_now, ShmemPerf::DRAM_QUEUE);
    // t_now += ddr_processing_time;
    // perf->updateTime(t_now, ShmemPerf::DRAM_BUS);
   
    // SubsecondTime cacheline_hw_access_latency = getDramAccessCost(pkt_time, pkt_size, requester, address, perf, false, false, false);
    // m_total_local_dram_hardware_latency += cacheline_hw_access_latency;
    // m_total_local_dram_hardware_latency_count++;
    // SubsecondTime t_now = pkt_time + cacheline_hw_access_latency;

    // // Update Memory Counters? 
    // //queue_delay = ddr_queue_delay;

    // SubsecondTime t_remote_queue_request = t_now;  // time of making queue request (after DRAM hardware access cost added)

    // ++m_num_recent_local_accesses;
    // if (((m_inflight_pages.find(phys_page) == m_inflight_pages.end()) && (m_inflight_pages_extra.find(phys_page) == m_inflight_pages_extra.end())) || m_r_enable_selective_moves) {
    //     // The phys_page is not included in m_inflight_pages and m_inflight_pages_extra, or m_r_enable_selective_moves is true: then total access latency = t_now - pkt_time
    //     SubsecondTime access_latency = t_now - pkt_time;
    //     m_total_local_access_latency += access_latency;
    //     m_total_access_latency += access_latency;
    //     // LOG_PRINT("getAccessLatency branch 1: %lu ns", access_latency.getNS());
    //     return access_latency;
    // } else {
    //     // The phys_page is an inflight page and m_r_enable_selective_moves is false
    //     //SubsecondTime current_time = std::min(Sim()->getClockSkewMinimizationServer()->getGlobalTime(), t_now);
    //     SubsecondTime access_latency;
    //     if (m_inflight_pages_extra.find(phys_page) != m_inflight_pages_extra.end()) {
    //         access_latency = m_inflight_pages_extra[phys_page] > t_now ? (m_inflight_pages_extra[phys_page] - pkt_time) : (t_now - pkt_time);
    //     } else {
    //         access_latency = m_inflight_pages[phys_page] > t_now ? (m_inflight_pages[phys_page] - pkt_time) : (t_now - pkt_time);
    //     }

    //     if (access_latency > (t_now - pkt_time)) {
    //         // The page is still in transit from remote to local memory
    //         m_inflight_hits++; 

    //         if (m_r_partition_queues != 0) {
    //             // Update dirty write buffer
    //             if (access_type == DramCntlrInterface::WRITE) {
    //                 UInt64 dirty_write_count = 0;
    //                 if (m_inflight_page_to_dirty_write_count.find(phys_page) != m_inflight_page_to_dirty_write_count.end())
    //                     dirty_write_count = m_inflight_page_to_dirty_write_count[phys_page];
    //                 dirty_write_count += 1;
    //                 // if (std::find(m_inflight_page_to_dirty_write_count.begin(), m_inflight_page_to_dirty_write_count.end(), phys_page) != m_inflight_page_to_dirty_write_count.end()) {
    //                 //     m_inflight_page_to_dirty_write_count[phys_page] = dirty_write_count;
    //                 // } else {
    //                 //     m_inflight_page_to_dirty_write_count.insert(phys_page, dirty_write_count);
    //                 // }
    //                 m_inflight_page_to_dirty_write_count[phys_page] = dirty_write_count;

    //                 m_dirty_write_buffer_size += 1;
    //             }

    //             double cacheline_queue_utilization_percentage;
    //             if (m_r_partition_queues == 1)
    //                 cacheline_queue_utilization_percentage = m_data_movement_2->getCachelineQueueUtilizationPercentage(t_now);
    //             else
    //                 cacheline_queue_utilization_percentage = m_data_movement->getCachelineQueueUtilizationPercentage(t_now);
    //             if (cacheline_queue_utilization_percentage > m_r_cacheline_queue_utilization_threshold) {
    //                 // Can't make additional cacheline request
    //                 ++m_redundant_moves_type2_cancelled_datamovement_queue_full;
    //             } else if (m_inflight_redundant[phys_page] < m_r_limit_redundant_moves) {
    //                 SubsecondTime add_cacheline_hw_access_latency = getDramAccessCost(t_now, pkt_size, requester, address, perf, true, false, false);
    //                 m_total_remote_dram_hardware_latency_cachelines += add_cacheline_hw_access_latency;
    //                 m_total_remote_dram_hardware_latency_cachelines_count++;

    //                 UInt32 size = pkt_size;
    //                 SubsecondTime cacheline_compression_latency = SubsecondTime::Zero();  // when cacheline compression is not enabled, this is always 0
    //                 if (m_use_compression)
    //                 {
    //                     if (m_r_cacheline_gran) {
    //                         if (m_r_partition_queues == 1)
    //                             m_compression_model->update_bandwidth_utilization(m_data_movement_2->getCachelineQueueUtilizationPercentage(t_now));
    //                         else if (m_r_partition_queues > 1)
    //                             m_compression_model->update_bandwidth_utilization(m_data_movement->getCachelineQueueUtilizationPercentage(t_now));
    //                         else  // ie partition queues off
    //                             m_compression_model->update_bandwidth_utilization(m_data_movement->getTotalQueueUtilizationPercentage(t_now));

    //                         UInt32 compressed_cache_lines;
    //                         cacheline_compression_latency = m_compression_model->compress(phys_page, m_cache_line_size, m_core_id, &size, &compressed_cache_lines);
    //                         if (m_cache_line_size > size)
    //                             bytes_saved += m_cache_line_size - size;
    //                         else
    //                             bytes_saved -= size - m_cache_line_size;

    //                         address_to_compressed_size[phys_page] = size;
    //                         address_to_num_cache_lines[phys_page] = compressed_cache_lines;
    //                         m_total_compression_latency += cacheline_compression_latency;
    //                     } else if (m_use_cacheline_compression) {
    //                         if (m_r_partition_queues == 1)
    //                             m_cacheline_compression_model->update_bandwidth_utilization(m_data_movement_2->getCachelineQueueUtilizationPercentage(t_now));
    //                         else if (m_r_partition_queues > 1)
    //                             m_cacheline_compression_model->update_bandwidth_utilization(m_data_movement->getCachelineQueueUtilizationPercentage(t_now));
    //                         else  // ie partition queues off
    //                             m_cacheline_compression_model->update_bandwidth_utilization(m_data_movement->getTotalQueueUtilizationPercentage(t_now));

    //                         UInt32 compressed_cache_lines;
    //                         cacheline_compression_latency = m_cacheline_compression_model->compress(phys_page, m_cache_line_size, m_core_id, &size, &compressed_cache_lines);
    //                         if (m_cache_line_size > size)
    //                             cacheline_bytes_saved += m_cache_line_size - size;
    //                         else
    //                             cacheline_bytes_saved -= size - m_cache_line_size;
    //                         address_to_num_cache_lines[phys_page] = compressed_cache_lines;
    //                         m_total_cacheline_compression_latency += cacheline_compression_latency;
    //                     }
    //                 }
    //                 // For clearer code logic, calculate decompression latency (but not adding it to anything) before computing the queue delay
    //                 SubsecondTime cacheline_decompression_latency = SubsecondTime::Zero();
    //                 // TODO: Currently model decompression by adding decompression latency to inflight page time
    //                 if (m_use_compression && (m_r_cacheline_gran || m_use_cacheline_compression))
    //                 {
    //                     CompressionModel *compression_model = m_r_cacheline_gran ? m_compression_model : m_cacheline_compression_model;
    //                     cacheline_decompression_latency = compression_model->decompress(phys_page, address_to_num_cache_lines[phys_page], m_core_id);
    //                     if (m_r_cacheline_gran)
    //                         m_total_decompression_latency += cacheline_decompression_latency;
    //                     else
    //                         m_total_cacheline_decompression_latency += cacheline_decompression_latency;
    //                 }

    //                 // Network transmission cost
    //                 SubsecondTime cacheline_network_processing_time = m_r_part2_bandwidth.getRoundedLatency(8*size);
                    
    //                 SubsecondTime add_cacheline_request_time = t_remote_queue_request + add_cacheline_hw_access_latency + cacheline_compression_latency;  // cacheline_compression_latency is 0 if compression not enabled
    //                 // Main difference when m_r_throttle_redundant_moves is true: reduce traffic sent through cacheline queue
    //                 if (m_r_throttle_redundant_moves) {
    //                     // Don't request cacheline via the cacheline queue if the inflight page arrives sooner
    //                     // But otherwise use the earlier arrival time to determine access_latency
    //                     SubsecondTime datamov_delay;
    //                     if (m_r_partition_queues == 1) {
    //                         datamov_delay = m_data_movement_2->computeQueueDelayNoEffect(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), QueueModel::CACHELINE, requester);
    //                     } else if (m_r_partition_queues == 2) {
    //                         datamov_delay = m_data_movement->computeQueueDelayNoEffect(add_cacheline_request_time, m_r_bus_bandwidth.getRoundedLatency(8*size), QueueModel::CACHELINE, requester);
    //                     } else if (m_r_partition_queues == 3) {
    //                         datamov_delay = m_data_movement->computeQueueDelayNoEffect(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), QueueModel::CACHELINE, requester);
    //                     } else if (m_r_partition_queues == 4) {
    //                         datamov_delay = m_data_movement->computeQueueDelayNoEffect(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), QueueModel::CACHELINE, requester);
    //                     }
    //                     datamov_delay += add_cacheline_hw_access_latency + cacheline_decompression_latency;  // decompression latency is 0 if not using cacheline compression
    //                     if ((t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time) < access_latency) {
    //                         if (m_r_partition_queues == 1) {
    //                             datamov_delay = m_data_movement_2->computeQueueDelayTrackBytes(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                         } else if (m_r_partition_queues == 2) {
    //                             datamov_delay = m_data_movement->computeQueueDelayTrackBytes(add_cacheline_request_time, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                         } else if (m_r_partition_queues == 3) {
    //                             std::vector<std::pair<UInt64, SubsecondTime>> updated_inflight_page_arrival_time_deltas;
    //                             datamov_delay = m_data_movement->computeQueueDelayTrackBytesPotentialPushback(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, updated_inflight_page_arrival_time_deltas, true, requester);
    //                             for (auto it = updated_inflight_page_arrival_time_deltas.begin(); it != updated_inflight_page_arrival_time_deltas.end(); ++it) {
    //                                 if (m_inflight_pages.count(it->first) && it->second > SubsecondTime::Zero()) {
    //                                     m_inflight_pages_delay_time += it->second;
    //                                     m_inflight_pages[it->first] += it->second;  // update arrival time if it's still an inflight page
    //                                     ++m_inflight_page_delayed;
    //                                 }
    //                             }
    //                         } else if (m_r_partition_queues == 4) {
    //                             datamov_delay = m_data_movement->computeQueueDelayTrackBytes(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                         }
    //                         datamov_delay += add_cacheline_hw_access_latency + cacheline_decompression_latency;
    //                         datamov_delay += cacheline_network_processing_time;
    //                         ++m_redundant_moves;
    //                         ++m_redundant_moves_type2;
    //                         --m_num_recent_local_accesses;
    //                         ++m_num_recent_remote_additional_accesses;  // this is now an additional remote access
    //                         m_inflight_redundant[phys_page] = m_inflight_redundant[phys_page] + 1;
    //                         // LOG_PRINT("getAccessLatency (local dram) inflight page saving of %lu ns", (access_latency-(t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time)).getNS());
    //                         m_redundant_moves_type2_time_savings += (access_latency - (t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time));
    //                         access_latency = datamov_delay + t_remote_queue_request + cacheline_compression_latency - pkt_time;
    //                     } else {
    //                         ++m_redundant_moves_type2_slower_than_page_arrival;
    //                     }
    //                 } else {
    //                     // Always request cacheline via the cacheline queue, then use the earlier arrival time to determine access_latency
    //                     SubsecondTime datamov_delay;
    //                     if (m_r_partition_queues == 1) {
    //                         datamov_delay = m_data_movement_2->computeQueueDelayTrackBytes(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                     } else if (m_r_partition_queues == 2) {
    //                         datamov_delay = m_data_movement->computeQueueDelayTrackBytes(add_cacheline_request_time, m_r_bus_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                     } else if (m_r_partition_queues == 3) {
    //                         std::vector<std::pair<UInt64, SubsecondTime>> updated_inflight_page_arrival_time_deltas;
    //                         datamov_delay = m_data_movement->computeQueueDelayTrackBytesPotentialPushback(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, updated_inflight_page_arrival_time_deltas, true, requester);
    //                         for (auto it = updated_inflight_page_arrival_time_deltas.begin(); it != updated_inflight_page_arrival_time_deltas.end(); ++it) {
    //                             if (m_inflight_pages.count(it->first) && it->second > SubsecondTime::Zero()) {
    //                                 m_inflight_pages_delay_time += it->second;
    //                                 m_inflight_pages[it->first] += it->second;  // update arrival time if it's still an inflight page
    //                                 ++m_inflight_page_delayed;
    //                             }
    //                         }
    //                     } else if (m_r_partition_queues == 4) {
    //                         datamov_delay = m_data_movement->computeQueueDelayTrackBytes(add_cacheline_request_time, m_r_part2_bandwidth.getRoundedLatency(8*size), size, QueueModel::CACHELINE, requester);
    //                     }
    //                     datamov_delay += add_cacheline_hw_access_latency + cacheline_decompression_latency;  // decompression latency is 0 if not using cacheline compression
    //                     datamov_delay += cacheline_network_processing_time;
    //                     ++m_redundant_moves;
    //                     ++m_redundant_moves_type2;
    //                     --m_num_recent_local_accesses;
    //                     ++m_num_recent_remote_additional_accesses;  // this is now an additional remote access
    //                     m_inflight_redundant[phys_page] = m_inflight_redundant[phys_page] + 1; 
    //                     if ((t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time) < access_latency) {
    //                         // LOG_PRINT("getAccessLatency (local dram) inflight page saving of %lu ns", (access_latency-(t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time)).getNS());
    //                         m_redundant_moves_type2_time_savings += (access_latency - (t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time));
    //                         access_latency = t_remote_queue_request + cacheline_compression_latency + datamov_delay - pkt_time;
    //                     }
    //                 }
    //             } else {
    //                 ++m_redundant_moves_type2_cancelled_limit_redundant_moves;
    //                 // std::cout << "Inflight page " << phys_page << " redundant moves > limit, = " << m_inflight_redundant[phys_page] << std::endl;
    //             }
    //         }
    //     }
    //     m_total_local_access_latency += access_latency;
    //     m_total_access_latency += access_latency;
    //     // LOG_PRINT("getAccessLatency branch 2: %lu ns", access_latency.getNS());
    //     return access_latency;  
    // }
}

bool
DramPerfModelDisaggRemoteOnly::isRemoteAccess(IntPtr address, core_id_t requester, DramCntlrInterface::access_t access_type) 
{
    UInt64 num_local_pages = m_localdram_size/m_page_size;
    if (m_r_cacheline_gran) // When we perform moves at cacheline granularity (should be disabled by default)
        num_local_pages = m_localdram_size/m_cache_line_size;  // Assuming 64bit cache line

    UInt64 phys_page =  address & ~((UInt64(1) << floorLog2(m_page_size)) - 1);
    if (m_r_cacheline_gran) 
        phys_page =  address & ~((UInt64(1) << floorLog2(m_cache_line_size)) - 1);

    if (m_r_mode == 0 || m_r_mode == 4) { // Static partitioning: no data movement and m_r_partitioning_ratio decides how many go where
        // if (std::find(m_local_pages.begin(), m_local_pages.end(), phys_page) != m_local_pages.end())
        if (m_local_pages.find(phys_page))
            return false;
        // else if (std::find(m_remote_pages.begin(), m_remote_pages.end(), phys_page) != m_remote_pages.end())
        else if (m_remote_pages.count(phys_page))
            return true;
        else if ( (unsigned int)(rand() % 100) < m_r_partitioning_ratio) {
            m_local_pages.push_back(phys_page);
            return false;
        } 
        else {
            m_remote_pages.insert(phys_page);
            return true; 
        }
    }
    else if (m_r_mode == 1 || m_r_mode == 2 || m_r_mode == 3 || m_r_mode == 5) {  // local DRAM as a cache 
        // if (std::find(m_local_pages.begin(), m_local_pages.end(), phys_page) != m_local_pages.end()) { // Is it in local DRAM?
        if (m_local_pages.find(phys_page)) { // Is it in local DRAM?
            m_local_pages.remove(phys_page); // LRU
            m_local_pages.push_back(phys_page);
            if (access_type == DramCntlrInterface::WRITE) {
                m_dirty_pages.insert(phys_page); // for unordered_set, the element is only inserted if it's not already in the container
            }
            return false;
        }
        // else if (std::find(m_remote_pages.begin(), m_remote_pages.end(), phys_page) != m_remote_pages.end()) {
        else if (m_remote_pages.count(phys_page)) {
            // printf("Remote page found: %lx\n", phys_page);
            if (m_use_throttled_pages_tracker && m_use_ideal_page_throttling && m_throttled_pages_tracker.count(phys_page)) {
                // This is a previously throttled page
                if (m_moved_pages_no_access_yet.size() > 0) {
                    UInt64 other_page = m_moved_pages_no_access_yet.front();  // for simplicity, choose first element
                    m_moved_pages_no_access_yet.pop_front();
                    // Do swap: mimic procedure for evicting other_page and replacing it with phys_page
                    // other_page hasn't been accessed yet so no need to check if it's dirty
                    m_local_pages.remove(other_page);
                    // if (!m_speed_up_simulation)
                    //     m_local_pages_remote_origin.erase(other_page);
                    if (std::find(m_remote_pages.begin(), m_remote_pages.end(), other_page) == m_remote_pages.end()) {
                        m_remote_pages.insert(other_page);  // other_page is not in remote_pages, add it back
                    }

                    m_local_pages.push_back(phys_page);
                    // if (!m_speed_up_simulation)
                    //     m_local_pages_remote_origin[phys_page] = 1;
                    if (m_r_exclusive_cache) {
                        m_remote_pages.erase(phys_page);
                    }
                    
                    auto it = m_inflight_pages.find(other_page);
                    if (it != m_inflight_pages.end()) {  // the other page was an inflight page
                        // m_inflight_pages.erase(phys_page);
                        m_data_movement->removeInflightPage(phys_page);  // TODO: replace with call that updates the map within queue model
                        m_inflight_pages[phys_page] = it->second;  // use the other page's arrival time
                        m_inflight_redundant[phys_page] = 0;

                        m_inflight_pages.erase(other_page);
                        m_data_movement->removeInflightPage(other_page);
                        m_inflight_redundant.erase(other_page);
                        ++m_ideal_page_throttling_swaps_inflight;
                    } else {
                        // For now, don't need to add this page to inflight pages
                        ++m_ideal_page_throttling_swaps_non_inflight;
                    }
                    return false;  // After swap, this is now a local access
                } else {
                    ++m_ideal_page_throttling_swap_unavailable;
                }
            }
            return true;
        }
        else {
            if (m_remote_init) { // Assuming all pages start off in remote memory
                m_remote_pages.insert(phys_page); 
                //    printf("Remote page found: %lx\n", phys_page); 
                return true;
            } else {
                if (m_local_pages.size() < num_local_pages) {
                    m_local_pages.push_back(phys_page);
                    return false; 
                } 
                else {
                    m_remote_pages.insert(phys_page); 
                    // printf("Remote page created: %lx\n", phys_page); 
                    return true;

                }
            }
        }  
    }
    return false;  
}

SubsecondTime 
DramPerfModelDisaggRemoteOnly::possiblyEvict(UInt64 phys_page, SubsecondTime t_now, core_id_t requester) 
{
    // Important note: phys_page is the current physical page being accessed before the call to this function,
    // NOT the page to be evicted!
    // This function can only evict one page per function call
    SubsecondTime sw_overhead = SubsecondTime::Zero();
    SubsecondTime evict_compression_latency = SubsecondTime::Zero();
    UInt64 evicted_page; 

    UInt64 num_local_pages = m_localdram_size/m_page_size;
    if (m_r_cacheline_gran)
        num_local_pages = m_localdram_size/m_cache_line_size;
        
    SubsecondTime page_network_processing_time = SubsecondTime::Zero();

    QueueModel::request_t queue_request_type = m_r_cacheline_gran ? QueueModel::CACHELINE : QueueModel::PAGE;
    SubsecondTime t_remote_queue_request = t_now;  // Use this instead of incrementing t_now throughout, if need to send page requests in parallel
    if (m_local_pages.size() > num_local_pages) {
        bool found = false;

        if (m_r_dontevictdirty) {
            auto i = m_local_pages.begin();
            for(unsigned int k = 0; k < m_local_pages.size()/2; ++i, ++k) {
                // if (std::find(m_dirty_pages.begin(), m_dirty_pages.end(), *i) == m_dirty_pages.end()) {
                if (!m_dirty_pages.count(*i)) {
                    // This is a non-dirty page
                    found = true;
                    evicted_page = *i; 
                    break;
                }
            }	
            // If a non-dirty page is found, just remove this page to make space
            if (found) {
                m_local_pages.remove(evicted_page);
                // if (!m_speed_up_simulation)
                //     m_local_pages_remote_origin.erase(evicted_page);
            }
        }

        // If found==false, remove the first page
        if (!found) {
            evicted_page = m_local_pages.front(); // Evict the least recently used page
            m_local_pages.pop_front();
            // if (!m_speed_up_simulation)
            //     m_local_pages_remote_origin.erase(evicted_page);
        }
        ++m_local_evictions; 

        if (m_r_simulate_sw_pagereclaim_overhead) 
            sw_overhead = SubsecondTime::NS() * 30000; 		

        // if (std::find(m_dirty_pages.begin(), m_dirty_pages.end(), evicted_page) != m_dirty_pages.end()) {
        if (m_dirty_pages.count(evicted_page)) {
            // The page to evict is dirty
            ++m_page_moves;
            ++m_writeback_pages;

            // Compress
            UInt32 size = m_r_cacheline_gran ? m_cache_line_size : m_page_size;
            if (m_use_compression)
            {
                m_compression_model->update_bandwidth_utilization(m_data_movement->getPageQueueUtilizationPercentage(t_now));
                if (m_r_partition_queues == 1 || m_r_partition_queues == 3 || m_r_partition_queues == 4)
                    m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_part_bandwidth, requester);
                else
                    m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_bus_bandwidth, requester);

                UInt32 gran_size = size;
                UInt32 compressed_cache_lines;
                SubsecondTime compression_latency = m_compression_model->compress(evicted_page, gran_size, m_core_id, &size, &compressed_cache_lines);
                if (gran_size > size)
                    bytes_saved += gran_size - size;
                else
                    bytes_saved -= size - gran_size;
 
                address_to_compressed_size[evicted_page] = size;
                address_to_num_cache_lines[evicted_page] = compressed_cache_lines;
                evict_compression_latency += compression_latency;
                m_total_compression_latency += compression_latency;
            }

            SubsecondTime page_datamovement_delay = SubsecondTime::Zero();
            if (m_r_simulate_datamov_overhead) { 
                if (m_r_partition_queues == 1) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } else if (m_r_partition_queues == 2) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } else if (m_r_partition_queues == 3) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } else if (m_r_partition_queues == 4) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } /* else if (m_r_cacheline_gran) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } */ else {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                }
            }

            // TODO: Currently model decompression by adding decompression latency to inflight page time
            if (m_use_compression)
            {
                SubsecondTime decompression_latency = m_compression_model->decompress(evicted_page, address_to_num_cache_lines[evicted_page], m_core_id);
                page_datamovement_delay += decompression_latency;
                m_total_decompression_latency += decompression_latency;
            }

            // if (std::find(m_remote_pages.begin(), m_remote_pages.end(), evicted_page) == m_remote_pages.end()) {
            if (!m_remote_pages.count(evicted_page)) {
                // The page to evict is not in remote_pages
                m_remote_pages.insert(evicted_page);
            }

            // Compute network transmission delay
            if (m_r_partition_queues > 0)
                page_network_processing_time = m_r_part_bandwidth.getRoundedLatency(8*size);
            else
                page_network_processing_time = m_r_bus_bandwidth.getRoundedLatency(8*size);

            m_inflightevicted_pages[evicted_page] = t_remote_queue_request + evict_compression_latency + page_datamovement_delay + page_network_processing_time;
            if (m_inflightevicted_pages.size() > m_max_inflightevicted_bufferspace)
                m_max_inflightevicted_bufferspace++;
            if (m_inflight_pages.size() + m_inflightevicted_pages.size() > m_max_total_bufferspace)
                m_max_total_bufferspace++;  // update stat

        // } else if (std::find(m_remote_pages.begin(), m_remote_pages.end(), evicted_page) == m_remote_pages.end()) {
        } else if (!m_remote_pages.count(evicted_page)) {
            // The page to evict is not dirty and not in remote memory
            m_remote_pages.insert(evicted_page);
            ++m_page_moves;

            // Compress
            UInt32 size = m_r_cacheline_gran ? m_cache_line_size : m_page_size;
            if (m_use_compression)
            {
                m_compression_model->update_bandwidth_utilization(m_data_movement->getPageQueueUtilizationPercentage(t_now));
                if (m_r_partition_queues == 1 || m_r_partition_queues == 3 || m_r_partition_queues == 4)
                    m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_part_bandwidth, requester);
                else
                    m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_bus_bandwidth, requester);

                UInt32 gran_size = size;
                UInt32 compressed_cache_lines;
                SubsecondTime compression_latency = m_compression_model->compress(evicted_page, gran_size, m_core_id, &size, &compressed_cache_lines);
                if (gran_size > size)
                    bytes_saved += gran_size - size;
                else
                    bytes_saved -= size - gran_size;
 
                address_to_compressed_size[evicted_page] = size;
                address_to_num_cache_lines[evicted_page] = compressed_cache_lines;
                evict_compression_latency += compression_latency;
                m_total_compression_latency += compression_latency;
            }

            SubsecondTime page_datamovement_delay = SubsecondTime::Zero();
            if (m_r_simulate_datamov_overhead) {
                if (m_r_partition_queues == 1) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } else if (m_r_partition_queues == 2) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } else if (m_r_partition_queues == 3) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } else if (m_r_partition_queues == 4) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } /* else if (m_r_cacheline_gran) {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                } */ else {
                    page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + evict_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
                }
            }

            // TODO: Currently model decompression by adding decompression latency to inflight page time
            if (m_use_compression)
            {
                SubsecondTime decompression_latency = m_compression_model->decompress(evicted_page, address_to_num_cache_lines[evicted_page], m_core_id);
                page_datamovement_delay += decompression_latency;
                m_total_decompression_latency += decompression_latency;
            }

            // Compute network transmission delay
            if (m_r_partition_queues > 0)
                page_network_processing_time = m_r_part_bandwidth.getRoundedLatency(8*size);
            else
                page_network_processing_time = m_r_bus_bandwidth.getRoundedLatency(8*size);

            m_inflightevicted_pages[evicted_page] = t_remote_queue_request + evict_compression_latency + page_datamovement_delay + page_network_processing_time;
            if (m_inflightevicted_pages.size() > m_max_inflightevicted_bufferspace)
                m_max_inflightevicted_bufferspace++;
            if (m_inflight_pages.size() + m_inflightevicted_pages.size() > m_max_total_bufferspace)
                m_max_total_bufferspace++;  // update stat
        }

        m_dirty_pages.erase(evicted_page);
    }
    // return sw_overhead + evict_compression_latency;
    return sw_overhead;  // latencies that are on the critical path
}


void 
DramPerfModelDisaggRemoteOnly::possiblyPrefetch(UInt64 phys_page, SubsecondTime t_now, core_id_t requester) 
{
    // Important note: phys_page is the current physical page being accessed before the call to this function,
    // NOT the page to be prefetched!
    if (!m_r_enable_nl_prefetcher) {
        return;
    }
    QueueModel::request_t queue_request_type = m_r_cacheline_gran ? QueueModel::CACHELINE : QueueModel::PAGE;
    SubsecondTime t_remote_queue_request = t_now;  // Use this instead of incrementing t_now throughout, to mimic sending page requests in parallel
    std::vector<UInt64> prefetch_page_candidates;
    m_prefetcher_model->pagePrefetchCandidates(phys_page, prefetch_page_candidates);
    for (auto it = prefetch_page_candidates.begin(); it != prefetch_page_candidates.end(); ++it) {
        if (m_data_movement->getPageQueueUtilizationPercentage(t_now) > m_r_page_queue_utilization_threshold) {
            // Cancel moving pages if the queue used for moving pages is already full
            ++m_prefetch_page_not_done_datamovement_queue_full;
            continue;  // could return here, but continue the loop to update stats for when prefetching was not done
        }
        UInt64 pref_page = *it;
        // if (std::find(m_local_pages.begin(), m_local_pages.end(), pref_page) != m_local_pages.end()) {
        if (m_local_pages.find(pref_page)) {
            ++m_prefetch_page_not_done_page_local_already;  // page already in m_local_pages
            continue;
        }
        // if (std::find(m_remote_pages.begin(), m_remote_pages.end(), pref_page) == m_remote_pages.end()) {
        if (!m_remote_pages.count(pref_page)) {
            if (m_prefetch_unencountered_pages) {
                m_remote_pages.insert(pref_page);  // hack for this page to be prefetched
            } else {
                ++m_prefetch_page_not_done_page_not_initialized;  // page not in m_remote_pages, means it's uninitialized? or just not seen yet by the system?
                continue;
            }
        }
        // pref_page is not in local_pages but in remote_pages, can prefetch
        ++m_page_prefetches;
        ++m_page_moves;

        SubsecondTime page_network_processing_time = SubsecondTime::Zero();

        // TODO: enable Hardware DRAM access cost for prefetched pages; need way to find ddr address from phys_page?
        // SubsecondTime page_hw_access_latency = getDramAccessCost(t_remote_queue_request, m_page_size, requester, address, perf, true, false, true);

        // Compress
        UInt32 size = m_r_cacheline_gran ? m_cache_line_size : m_page_size;
        SubsecondTime page_compression_latency = SubsecondTime::Zero();  // when page compression is not enabled, this is always 0
        if (m_use_compression)
        {
            if (m_use_r_compressed_pages && address_to_compressed_size.find(phys_page) != address_to_compressed_size.end()) {
                size = address_to_compressed_size[phys_page];
            } else {
                m_compression_model->update_bandwidth_utilization(m_data_movement->getPageQueueUtilizationPercentage(t_now));
                if (m_r_partition_queues == 1 || m_r_partition_queues == 3 || m_r_partition_queues == 4)
                    m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_part_bandwidth, requester);
                else
                    m_compression_model->update_queue_model(m_data_movement, t_now, &m_r_bus_bandwidth, requester);

                UInt32 gran_size = size;
                UInt32 compressed_cache_lines;
                page_compression_latency = m_compression_model->compress(pref_page, gran_size, m_core_id, &size, &compressed_cache_lines);
                if (gran_size > size)
                    bytes_saved += gran_size - size;
                else
                    bytes_saved -= size - gran_size;

                address_to_compressed_size[pref_page] = size;
                address_to_num_cache_lines[pref_page] = compressed_cache_lines;
                m_total_compression_latency += page_compression_latency;
            }
        }

        SubsecondTime page_datamovement_delay = SubsecondTime::Zero();
        if (m_r_simulate_datamov_overhead) {
            if (m_r_partition_queues == 1) {
                page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
            } else if (m_r_partition_queues == 2) {
                page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
            } else if (m_r_partition_queues == 3) {
                page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester, true, pref_page);
            } else if (m_r_partition_queues == 4) {
                page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_compression_latency, m_r_part_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
            } else {
                page_datamovement_delay = m_data_movement->computeQueueDelayTrackBytes(t_remote_queue_request + page_compression_latency, m_r_bus_bandwidth.getRoundedLatency(8*size), size, queue_request_type, requester);
            }
        }

        // Compute network transmission delay
        if (m_r_partition_queues > 0)
            page_network_processing_time = m_r_part_bandwidth.getRoundedLatency(8*size);
        else
            page_network_processing_time = m_r_bus_bandwidth.getRoundedLatency(8*size);

        // TODO: Currently model decompression by adding decompression latency to inflight page time
        if (m_use_compression)
        {
            SubsecondTime decompression_latency = m_compression_model->decompress(pref_page, address_to_num_cache_lines[pref_page], m_core_id);
            page_datamovement_delay += decompression_latency;
            m_total_decompression_latency += decompression_latency;
        }

        // assert(std::find(m_local_pages.begin(), m_local_pages.end(), pref_page) == m_local_pages.end()); 
        // assert(std::find(m_remote_pages.begin(), m_remote_pages.end(), pref_page) != m_remote_pages.end()); 
        // Commenting out since earlier in this loop, these checks were already made
        // assert(!m_local_pages.find(pref_page)); 
        // assert(m_remote_pages.count(pref_page)); 
        m_local_pages.push_back(pref_page);
        if (m_r_exclusive_cache)
            m_remote_pages.erase(pref_page);
        // if (!m_speed_up_simulation)
        //     m_local_pages_remote_origin[pref_page] = 1;
        // m_inflight_pages.erase(pref_page);
        m_inflight_pages[pref_page] = t_remote_queue_request + page_compression_latency + page_datamovement_delay + page_network_processing_time;  // use max of this time with Sim()->getClockSkewMinimizationServer()->getGlobalTime() here as well?
        m_inflight_redundant[pref_page] = 0; 
        if (m_inflight_pages.size() > m_max_inflight_bufferspace)
            m_max_inflight_bufferspace++;
        if (m_inflight_pages.size() + m_inflightevicted_pages.size() > m_max_total_bufferspace)
            m_max_total_bufferspace++;  // update stat
        possiblyEvict(phys_page, t_remote_queue_request, requester);
    }
}