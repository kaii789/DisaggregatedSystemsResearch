#ifndef __COMPRESSION_MODEL_LCP_H__
#define __COMPRESSION_MODEL_LCP_H__

#include "compression_model.h"

class CompressionModelLCP : public CompressionModel
{
public:
   CompressionModelLCP(String name, UInt32 page_size, UInt32 cache_line_size);
   ~CompressionModelLCP();

   SubsecondTime compress(IntPtr addr, size_t data_size, core_id_t core_id, UInt32 *compressed_page_size, UInt32 *compressed_cache_lines);
   SubsecondTime decompress(IntPtr addr, UInt32 compressed_cache_lines, core_id_t core_id);

   SubsecondTime compress_multipage(std::vector<UInt64> addr_list, UInt32 num_pages, core_id_t core_id, UInt32 *compressed_multipage_size, std::map<UInt64, UInt32> *address_to_num_cache_lines);
   SubsecondTime decompress_multipage(std::vector<UInt64> addr_list, UInt32 num_pages, core_id_t core_id, std::map<UInt64, UInt32> *address_to_num_cache_lines);

private:
    String m_name;
    UInt32 m_page_size;
    UInt32 m_cache_line_size;
    UInt32 m_cacheline_count;
    UInt32 *m_compressed_cache_line_sizes;

    CompressionModel *m_compression_model;

    // Compression latency per cache line
    UInt32 m_compression_latency = 3;
    // Decompression latency per cache line
    UInt32 m_decompression_latency = 5;

    UInt32 m_target_compressed_cacheline_size[4] = {16, 21, 32, 44};
};

#endif /* __COMPRESSION_MODEL_LCP_H__ */
