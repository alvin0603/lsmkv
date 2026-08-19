#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>

namespace
{
    std::uint64_t percentile(const std::vector<std::uint64_t>& samples, double value)
    {
        if(samples.empty())
            return 0;
        const std::size_t rank = static_cast<std::size_t>(std::ceil(value * static_cast<double>(samples.size())));
        return samples[rank - 1];
    }
    void writeCsvHeader(std::ostream& output)
    {
        output << "seed,operations,warmup_operations,keys,read_percent,write_percent,delete_percent,key_distribution,zipf_theta,min_value_size,max_value_size,sync_mode,sync_interval,memory_budget_bytes,memtable_size_bytes,block_cache_capacity_bytes,bloom_bits_per_key,bloom_hash_count,bloom_memory_bytes,total_memory_bytes,reads,writes,deletes,elapsed_ns,throughput_ops_per_second,read_p50_ns,read_p99_ns,read_p999_ns,write_p50_ns,write_p99_ns,write_p999_ns,delete_p50_ns,delete_p99_ns,delete_p999_ns,wal_bytes_written,sstable_bytes_written,user_bytes_written,write_amplification,live_data_bytes,sstable_size_bytes,space_amplification,cache_hits,cache_misses,cache_hit_rate,compaction_count,compaction_bytes_read,compaction_bytes_written\n";
    }
}

namespace lsmkv::bench
{
    void LatencyHistogram::record(std::uint64_t latency_ns)
    {
        samples_.push_back(latency_ns);
    }
    std::size_t LatencyHistogram::size() const
    {
        return samples_.size();
    }
    LatencySummary LatencyHistogram::summarize() const
    {
        if(samples_.empty())
            return {};
        std::vector<std::uint64_t> sorted_samples = samples_;
        std::sort(sorted_samples.begin(), sorted_samples.end());
        LatencySummary summary;
        summary.p50_ns = percentile(sorted_samples, 0.5);
        summary.p99_ns = percentile(sorted_samples, 0.99);
        summary.p999_ns = percentile(sorted_samples, 0.999);
        return summary;
    }
    double calculateWriteAmplification(std::uint64_t wal_bytes, std::uint64_t sstable_bytes, std::uint64_t user_bytes)
    {
        if(user_bytes == 0)
            return 0.0;
        return static_cast<double>(wal_bytes) / static_cast<double>(user_bytes) + static_cast<double>(sstable_bytes) / static_cast<double>(user_bytes);
    }
    double calculateSpaceAmplification(std::uint64_t sstable_bytes, std::uint64_t live_data_bytes)
    {
        if(live_data_bytes == 0)
            return 0.0;
        return static_cast<double>(sstable_bytes) / static_cast<double>(live_data_bytes);
    }
    bool writeCsv(std::string_view path, const BenchmarkResult& result)
    {
        const std::filesystem::path output_path{std::string(path)};
        std::error_code error;
        const bool write_header = !std::filesystem::exists(output_path, error) || (!error && std::filesystem::file_size(output_path, error) == 0);
        if(error)
            return false;
        std::ofstream output(output_path, std::ios::app);
        if(!output.is_open())
            return false;
        if(write_header)
            writeCsvHeader(output);
        output << std::setprecision(17)
               << result.seed << ',' << result.operation_count << ',' << result.warmup_operation_count << ',' << result.key_count << ','
               << result.read_percent << ',' << result.write_percent << ',' << result.delete_percent << ',' << result.key_distribution << ','
               << result.zipf_theta << ',' << result.min_value_size << ',' << result.max_value_size << ',' << result.sync_mode << ',' << result.sync_interval << ','
               << result.memory_budget_bytes << ',' << result.memtable_size_bytes << ',' << result.block_cache_capacity_bytes << ',' << result.bloom_bits_per_key << ','
               << result.bloom_hash_count << ',' << result.bloom_memory_bytes << ',' << result.total_memory_bytes << ','
               << result.read_count << ',' << result.write_count << ',' << result.delete_count << ',' << result.elapsed_ns << ',' << result.throughput_ops_per_second << ','
               << result.read_latency.p50_ns << ',' << result.read_latency.p99_ns << ',' << result.read_latency.p999_ns << ','
               << result.write_latency.p50_ns << ',' << result.write_latency.p99_ns << ',' << result.write_latency.p999_ns << ','
               << result.delete_latency.p50_ns << ',' << result.delete_latency.p99_ns << ',' << result.delete_latency.p999_ns << ','
               << result.wal_bytes_written << ',' << result.sstable_bytes_written << ',' << result.user_bytes_written << ',' << result.write_amplification << ','
               << result.live_data_bytes << ',' << result.sstable_size_bytes << ',' << result.space_amplification << ','
               << result.cache_hit_count << ',' << result.cache_miss_count << ',' << result.cache_hit_rate << ','
               << result.compaction_count << ',' << result.compaction_bytes_read << ',' << result.compaction_bytes_written << '\n';
        return output.good();
    }
    bool writeJson(std::string_view path, const BenchmarkResult& result)
    {
        const std::filesystem::path output_path{std::string(path)};
        std::ofstream output(output_path, std::ios::trunc);
        if(!output.is_open())
            return false;
        output << std::setprecision(17)
               << "{\n"
               << "    \"seed\": " << result.seed << ",\n"
               << "    \"operations\": " << result.operation_count << ",\n"
               << "    \"warmup_operations\": " << result.warmup_operation_count << ",\n"
               << "    \"keys\": " << result.key_count << ",\n"
               << "    \"read_percent\": " << result.read_percent << ",\n"
               << "    \"write_percent\": " << result.write_percent << ",\n"
               << "    \"delete_percent\": " << result.delete_percent << ",\n"
               << "    \"key_distribution\": \"" << result.key_distribution << "\",\n"
               << "    \"zipf_theta\": " << result.zipf_theta << ",\n"
               << "    \"min_value_size\": " << result.min_value_size << ",\n"
               << "    \"max_value_size\": " << result.max_value_size << ",\n"
               << "    \"sync_mode\": \"" << result.sync_mode << "\",\n"
               << "    \"sync_interval\": " << result.sync_interval << ",\n"
               << "    \"memory_budget_bytes\": " << result.memory_budget_bytes << ",\n"
               << "    \"memtable_size_bytes\": " << result.memtable_size_bytes << ",\n"
               << "    \"block_cache_capacity_bytes\": " << result.block_cache_capacity_bytes << ",\n"
               << "    \"bloom_bits_per_key\": " << result.bloom_bits_per_key << ",\n"
               << "    \"bloom_hash_count\": " << result.bloom_hash_count << ",\n"
               << "    \"bloom_memory_bytes\": " << result.bloom_memory_bytes << ",\n"
               << "    \"total_memory_bytes\": " << result.total_memory_bytes << ",\n"
               << "    \"reads\": " << result.read_count << ",\n"
               << "    \"writes\": " << result.write_count << ",\n"
               << "    \"deletes\": " << result.delete_count << ",\n"
               << "    \"elapsed_ns\": " << result.elapsed_ns << ",\n"
               << "    \"throughput_ops_per_second\": " << result.throughput_ops_per_second << ",\n"
               << "    \"read_p50_ns\": " << result.read_latency.p50_ns << ",\n"
               << "    \"read_p99_ns\": " << result.read_latency.p99_ns << ",\n"
               << "    \"read_p999_ns\": " << result.read_latency.p999_ns << ",\n"
               << "    \"write_p50_ns\": " << result.write_latency.p50_ns << ",\n"
               << "    \"write_p99_ns\": " << result.write_latency.p99_ns << ",\n"
               << "    \"write_p999_ns\": " << result.write_latency.p999_ns << ",\n"
               << "    \"delete_p50_ns\": " << result.delete_latency.p50_ns << ",\n"
               << "    \"delete_p99_ns\": " << result.delete_latency.p99_ns << ",\n"
               << "    \"delete_p999_ns\": " << result.delete_latency.p999_ns << ",\n"
               << "    \"wal_bytes_written\": " << result.wal_bytes_written << ",\n"
               << "    \"sstable_bytes_written\": " << result.sstable_bytes_written << ",\n"
               << "    \"user_bytes_written\": " << result.user_bytes_written << ",\n"
               << "    \"write_amplification\": " << result.write_amplification << ",\n"
               << "    \"live_data_bytes\": " << result.live_data_bytes << ",\n"
               << "    \"sstable_size_bytes\": " << result.sstable_size_bytes << ",\n"
               << "    \"space_amplification\": " << result.space_amplification << ",\n"
               << "    \"cache_hits\": " << result.cache_hit_count << ",\n"
               << "    \"cache_misses\": " << result.cache_miss_count << ",\n"
               << "    \"cache_hit_rate\": " << result.cache_hit_rate << ",\n"
               << "    \"compaction_count\": " << result.compaction_count << ",\n"
               << "    \"compaction_bytes_read\": " << result.compaction_bytes_read << ",\n"
               << "    \"compaction_bytes_written\": " << result.compaction_bytes_written << "\n"
               << "}\n";
        return output.good();
    }
}
