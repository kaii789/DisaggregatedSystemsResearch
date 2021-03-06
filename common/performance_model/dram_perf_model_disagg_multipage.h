#ifndef __DRAM_PERF_MODEL_DISAGG_MULTIPAGE_H__
#define __DRAM_PERF_MODEL_DISAGG_MULTIPAGE_H__

#include "dram_perf_model.h"
#include "queue_model.h"
#include "compression_model.h"
#include "fixed_types.h"
#include "subsecond_time.h"
#include "dram_cntlr_interface.h"

#include <vector>
#include <bitset>
#include <map>
#include <list>
#include <algorithm>

class DramPerfModelDisaggMultipage : public DramPerfModel
{
    private:
        const core_id_t m_core_id;
        const AddressHomeLookup* m_address_home_lookup;
        const UInt32 m_num_banks;       // number of banks in a rank
        const UInt32 m_num_banks_log2;
        const UInt32 m_num_bank_groups; // number of bank groups in a rank
        const UInt32 m_num_ranks;
        const UInt32 m_rank_offset;
        const UInt32 m_num_channels;
        const UInt32 m_channel_offset;
        const UInt32 m_home_lookup_bit;
        const UInt32 m_total_ranks;
        const UInt32 m_banks_per_channel;
        const UInt32 m_banks_per_bank_group;
        const UInt32 m_total_banks;
        const UInt32 m_total_bank_groups;
        const UInt32 m_data_bus_width;  // bus between dram and memory controller
        const UInt32 m_dram_speed;      // MHz, 533, 667, etc.
        const UInt32 m_dram_page_size;  // dram page size in bytes
        const UInt32 m_dram_page_size_log2;
        const bool m_open_page_mapping;
        const UInt32 m_column_offset;
        const UInt32 m_column_hi_offset;
        const UInt32 m_bank_offset;
        const bool m_randomize_address;
        const UInt32 m_randomize_offset;
        const UInt32 m_column_bits_shift; // Position of column bits for closed-page mapping (after cutting interleaving/channel/rank/bank from bottom)
        const ComponentBandwidth m_bus_bandwidth;
        const ComponentBandwidth m_r_bus_bandwidth;   // Remote
        const ComponentBandwidth m_r_part_bandwidth;  // Remote - Partitioned Queues => Page Queue
        const ComponentBandwidth m_r_part2_bandwidth; // Remote - Partitioned Queues => Cacheline Queue
        const SubsecondTime m_bank_keep_open;
        const SubsecondTime m_bank_open_delay;
        const SubsecondTime m_bank_close_delay;
        const SubsecondTime m_dram_access_cost;
        const SubsecondTime m_intercommand_delay;       // Rank availability
        const SubsecondTime m_intercommand_delay_short; // Rank availability
        const SubsecondTime m_intercommand_delay_long;  // Bank group availability
        const SubsecondTime m_controller_delay;         // Average pipeline delay for various DDR controller stages
        const SubsecondTime m_refresh_interval;         // tRFCI
        const SubsecondTime m_refresh_length;           // tRFC
        const SubsecondTime m_r_added_latency; // Additional remote latency
        const UInt32 m_r_datamov_threshold; // Move data if greater than yy
        const UInt32 m_cache_line_size;
        const UInt32 m_page_size; // Memory page size (in bytes) in disagg.cc (different from ddr page size)
        const UInt32 m_localdram_size; // Local DRAM size
        const bool m_enable_remote_mem; // Enable remote memory with the same DDR type as local for now
        const bool m_r_simulate_tlb_overhead; // Simulate tlb overhead
        const bool m_r_simulate_datamov_overhead; // Simulate datamovement overhead for remote memory
        const UInt32 m_r_mode ; // Randomly assigned = 0; Cache = 1 (local DRAM as a cache) 
        const UInt32 m_r_partitioning_ratio ; // % in local memory 
        const bool m_r_simulate_sw_pagereclaim_overhead; // Simulate tlb overhead
        const bool m_r_exclusive_cache; // Simulate tlb overhead
        const bool m_remote_init; // All pages are initially allocated to remote memory
        const bool m_r_enable_nl_prefetcher; // Enable prefetcher to prefetch pages from remote DRAM to local DRAM
        const UInt32 m_r_disturbance_factor; // Other systems using the remote memory and creating disturbance
        const bool m_r_dontevictdirty; // Do not evict dirty data
        const bool m_r_enable_selective_moves; 
        const UInt32 m_r_partition_queues; // Enable partitioned queues
        double m_r_cacheline_queue_fraction; // The fraction of remote bandwidth used for the cacheline queue (decimal between 0 and 1) 
        const bool m_r_cacheline_gran; // Move data and operate in cacheline granularity
        const UInt32 m_r_reserved_bufferspace; // Max % of local DRAM that can be reserved for pages in transit
        const UInt32 m_r_limit_redundant_moves; 
        const bool m_r_throttle_redundant_moves;
        const bool m_r_use_separate_queue_model;  // Whether to use the separate remote queue model

        // Local Memory
        std::vector<QueueModel*> m_queue_model;
        std::vector<QueueModel*> m_rank_avail;
        std::vector<QueueModel*> m_bank_group_avail;

        QueueModel* m_data_movement;        // Normally, this is the combined queue for pages and cachelines. When partitioned queues are enabled, this is the page queue
        QueueModel* m_data_movement_2;      // When partitioned queues are enabled, this is the cacheline queue

        struct BankInfo
        {
            IntPtr open_page;
            SubsecondTime t_avail;
        };
        std::vector<BankInfo> m_banks;

        // Remote memory
        std::vector<QueueModel*> m_r_queue_model;
        std::vector<QueueModel*> m_r_rank_avail;
        std::vector<QueueModel*> m_r_bank_group_avail;

        std::vector<BankInfo> m_r_banks;

        std::map<UInt64, UInt32> m_remote_access_tracker; 
        std::list<UInt64> m_local_pages; // Pages of local memory
        std::list<UInt64> m_remote_pages; // Pages of remote memory
        std::list<UInt64> m_dirty_pages; // Dirty pages of local memory
        std::map<UInt64, SubsecondTime> m_inflight_pages; // Inflight pages that are being transferred from remote memory to local memory
        std::map<UInt64, UInt32> m_inflight_redundant; 
        std::map<UInt64, SubsecondTime> m_inflightevicted_pages; // Inflight pages that are being transferred from local memory to remote memory

        // TODO: Compression
        bool m_use_compression;
        CompressionModel *m_compression_model;
        UInt64 bytes_saved = 0;
        SubsecondTime m_total_compression_latency = SubsecondTime::Zero();
        SubsecondTime m_total_decompression_latency = SubsecondTime::Zero();
        std::map<IntPtr, UInt32> address_to_compressed_size;
        std::map<IntPtr, UInt32> address_to_num_cache_lines;

        // TODO: Multipage
        std::vector<UInt64> page_buffer;
        UInt32 m_page_buffer_capacity;

        // Variables to keep track of stats
        UInt64 m_dram_page_hits;
        UInt64 m_dram_page_empty;
        UInt64 m_dram_page_closing;
        UInt64 m_dram_page_misses;
        UInt64 m_remote_reads;
        UInt64 m_remote_writes;
        UInt64 m_page_moves;
        UInt64 m_page_prefetches;
        UInt64 m_inflight_hits;
        UInt64 m_writeback_pages;
        UInt64 m_local_evictions;
        UInt64 m_extra_pages;
        UInt64 m_redundant_moves;                   // number of times both a cacheline and its containing page are requested together
        UInt64 m_redundant_moves_type1;
        UInt64 m_redundant_moves_type1_cache_slower_than_page;
        UInt64 m_redundant_moves_type2;
        UInt64 m_cacheline_queue_request_cancelled; // number of times a cacheline queue request is cancelled (currently, due to m_r_limit_redundant_moves)
        UInt64 m_max_bufferspace;                   // the maximum number of localdram pages actually used to back inflight and inflight_evicted pages 
        UInt64 m_move_page_cancelled_bufferspace_full;         // the number of times moving a remote page to local was cancelled due to localdram bufferspace being full
        UInt64 m_move_page_cancelled_datamovement_queue_full;  // the number of times moving a remote page to local was cancelled due to the queue for pages being full
        std::map<UInt64, UInt32> m_page_usage_map;  // track number of times each phys page is accessed
        UInt64 m_unique_pages_accessed;             // track number of unique pages accessed
        SubsecondTime m_redundant_moves_type1_time_savings;
        SubsecondTime m_redundant_moves_type2_time_savings;

        SubsecondTime m_total_queueing_delay;
        SubsecondTime m_total_access_latency;
        SubsecondTime m_total_local_access_latency;
        SubsecondTime m_total_remote_access_latency;

        void parseDeviceAddress(IntPtr address, UInt32 &channel, UInt32 &rank, UInt32 &bank_group, UInt32 &bank, UInt32 &column, UInt64 &dram_page);
        UInt64 parseAddressBits(UInt64 address, UInt32 &data, UInt32 offset, UInt32 size, UInt64 base_address);
        SubsecondTime possiblyEvict(UInt64 phys_page, SubsecondTime pkt_time, core_id_t requester); 
        void possiblyPrefetch(UInt64 phys_page, SubsecondTime pkt_time, core_id_t requester); 

    public:
        DramPerfModelDisaggMultipage(core_id_t core_id, UInt32 cache_block_size, AddressHomeLookup* address_home_lookup);

        ~DramPerfModelDisaggMultipage();
        void finalizeStats() { return; }
        void updateBandwidth() {return; }

        bool isRemoteAccess(IntPtr address, core_id_t requester, DramCntlrInterface::access_t access_type); 
        SubsecondTime getAccessLatencyRemote(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf);

        SubsecondTime getAccessLatency(SubsecondTime pkt_time, UInt64 pkt_size, core_id_t requester, IntPtr address, DramCntlrInterface::access_t access_type, ShmemPerf *perf);
};

#endif /* __DRAM_PERF_MODEL_DISAGG_MULTIPAGE_H__ */
