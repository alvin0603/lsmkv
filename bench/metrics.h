#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsmkv::bench
{
    struct LatencySummary
    {
        std::uint64_t p50_ns = 0;
        std::uint64_t p99_ns = 0;
        std::uint64_t p999_ns = 0;
    };
    class LatencyHistogram
    {
        public:
            void record(std::uint64_t latency_ns);
            std::size_t size() const;
            LatencySummary summarize() const;
        private:
            std::vector<std::uint64_t> samples_;
    };
    struct BenchmarkResult
    {
        std::uint64_t seed = 0;
        std::uint64_t operation_count = 0;
        std::uint64_t warmup_operation_count = 0;
        std::uint64_t key_count = 0;
        std::uint32_t read_percent = 0;
        std::uint32_t write_percent = 0;
        std::uint32_t delete_percent = 0;
        std::string key_distribution;
        double zipf_theta = 0.0;
        std::size_t min_value_size = 0;
        std::size_t max_value_size = 0;
        std::string sync_mode;
        std::size_t sync_interval = 0;
        std::uint64_t memory_budget_bytes = 0;
        std::uint64_t memtable_size_bytes = 0;
        std::uint64_t block_cache_capacity_bytes = 0;
        std::uint64_t bloom_bits_per_key = 0;
        std::uint32_t bloom_hash_count = 0;
        std::uint64_t bloom_memory_bytes = 0;
        std::uint64_t total_memory_bytes = 0;
        std::uint64_t read_count = 0;
        std::uint64_t write_count = 0;
        std::uint64_t delete_count = 0;
        std::uint64_t elapsed_ns = 0;
        double throughput_ops_per_second = 0.0;
        LatencySummary read_latency;
        LatencySummary write_latency;
        LatencySummary delete_latency;
        std::uint64_t wal_bytes_written = 0;
        std::uint64_t sstable_bytes_written = 0;
        std::uint64_t user_bytes_written = 0;
        double write_amplification = 0.0;
        std::uint64_t live_data_bytes = 0;
        std::uint64_t sstable_size_bytes = 0;
        double space_amplification = 0.0;
        std::uint64_t cache_hit_count = 0;
        std::uint64_t cache_miss_count = 0;
        double cache_hit_rate = 0.0;
        std::uint64_t compaction_count = 0;
        std::uint64_t compaction_bytes_read = 0;
        std::uint64_t compaction_bytes_written = 0;
    };
    double calculateWriteAmplification(std::uint64_t wal_bytes, std::uint64_t sstable_bytes, std::uint64_t user_bytes);
    double calculateSpaceAmplification(std::uint64_t sstable_bytes, std::uint64_t live_data_bytes);
    bool writeCsv(std::string_view path, const BenchmarkResult& result);
    bool writeJson(std::string_view path, const BenchmarkResult& result);
}
