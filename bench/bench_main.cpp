#include "workload.h"
#include "metrics.h"
#include <lsmkv/db.h>

#include <chrono>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
    enum class OutputFormat
    {
        kCsv,
        kJson
    };
    struct BenchOptions
    {
        lsmkv::bench::WorkloadConfig workload;
        lsmkv::SyncMode sync_mode = lsmkv::SyncMode::kSyncOff;
        std::size_t sync_interval = 1;
        std::uint64_t warmup_operation_count = 0;
        std::string db_path;
        std::string output_path;
        OutputFormat output_format = OutputFormat::kCsv;
        bool show_help = false;
    };
    template<typename T>
    bool parseUnsigned(std::string_view input, T& value)
    {
        if(input.empty())
            return false;
        T parsed = 0;
        const auto result = std::from_chars(input.data(), input.data() + input.size(), parsed);
        if(result.ec != std::errc{} || result.ptr != input.data() + input.size())
            return false;
        value = parsed;
        return true;
    }
    bool parseDouble(std::string_view input, double& value)
    {
        if(input.empty())
            return false;
        const std::string text(input);
        char* end = nullptr;
        errno = 0;
        const double parsed = std::strtod(text.c_str(), &end);
        if(errno == ERANGE || end != text.c_str() + text.size())
            return false;
        value = parsed;
        return true;
    }
    void printUsage()
    {
        std::cout << "usage: lsmkv_bench --db <path> [options]\n";
        std::cout << "  --operations <count>\n";
        std::cout << "  --keys <count>\n";
        std::cout << "  --reads <percent>\n";
        std::cout << "  --writes <percent>\n";
        std::cout << "  --deletes <percent>\n";
        std::cout << "  --distribution <uniform|sequential|zipfian>\n";
        std::cout << "  --zipf-theta <value>\n";
        std::cout << "  --value-size <bytes>\n";
        std::cout << "  --value-min-size <bytes>\n";
        std::cout << "  --value-max-size <bytes>\n";
        std::cout << "  --seed <value>\n";
        std::cout << "  --sync-mode <off|every-write|every-n>\n";
        std::cout << "  --sync-interval <count>\n";
        std::cout << "  --warmup-operations <count>\n";
        std::cout << "  --output <path>\n";
        std::cout << "  --format <csv|json>\n";
    }
    bool parseArguments(int argc, char* argv[], BenchOptions& options)
    {
        for(int i = 1; i < argc; i++)
        {
            const std::string_view argument = argv[i];
            if(argument == "--help")
            {
                options.show_help = true;
                continue;
            }
            if(i + 1 >= argc)
            {
                std::cerr << "missing value for " << argument << '\n';
                return false;
            }
            const std::string_view value = argv[++i];
            if(argument == "--db")
                options.db_path = value;
            else if(argument == "--operations")
            {
                if(!parseUnsigned(value, options.workload.operation_count))
                    return false;
            }
            else if(argument == "--keys")
            {
                if(!parseUnsigned(value, options.workload.key_count))
                    return false;
            }
            else if(argument == "--reads")
            {
                if(!parseUnsigned(value, options.workload.read_percent))
                    return false;
            }
            else if(argument == "--writes")
            {
                if(!parseUnsigned(value, options.workload.write_percent))
                    return false;
            }
            else if(argument == "--deletes")
            {
                if(!parseUnsigned(value, options.workload.delete_percent))
                    return false;
            }
            else if(argument == "--distribution")
            {
                if(value == "uniform")
                    options.workload.key_distribution = lsmkv::bench::KeyDistribution::kUniform;
                else if(value == "sequential")
                    options.workload.key_distribution = lsmkv::bench::KeyDistribution::kSequential;
                else if(value == "zipfian")
                    options.workload.key_distribution = lsmkv::bench::KeyDistribution::kZipfian;
                else
                    return false;
            }
            else if(argument == "--zipf-theta")
            {
                if(!parseDouble(value, options.workload.zipf_theta))
                    return false;
            }
            else if(argument == "--value-size")
            {
                std::size_t value_size = 0;
                if(!parseUnsigned(value, value_size))
                    return false;
                options.workload.min_value_size = value_size;
                options.workload.max_value_size = value_size;
            }
            else if(argument == "--value-min-size")
            {
                if(!parseUnsigned(value, options.workload.min_value_size))
                    return false;
            }
            else if(argument == "--value-max-size")
            {
                if(!parseUnsigned(value, options.workload.max_value_size))
                    return false;
            }
            else if(argument == "--seed")
            {
                if(!parseUnsigned(value, options.workload.seed))
                    return false;
            }
            else if(argument == "--sync-mode")
            {
                if(value == "off")
                    options.sync_mode = lsmkv::SyncMode::kSyncOff;
                else if(value == "every-write")
                    options.sync_mode = lsmkv::SyncMode::kSyncEveryWrite;
                else if(value == "every-n")
                    options.sync_mode = lsmkv::SyncMode::kSyncEveryN;
                else
                    return false;
            }
            else if(argument == "--sync-interval")
            {
                if(!parseUnsigned(value, options.sync_interval))
                    return false;
            }
            else if(argument == "--warmup-operations")
            {
                if(!parseUnsigned(value, options.warmup_operation_count))
                    return false;
            }
            else if(argument == "--output")
                options.output_path = value;
            else if(argument == "--format")
            {
                if(value == "csv")
                    options.output_format = OutputFormat::kCsv;
                else if(value == "json")
                    options.output_format = OutputFormat::kJson;
                else
                    return false;
            }
            else
            {
                std::cerr << "unknown option: " << argument << '\n';
                return false;
            }
        }
        if(options.show_help)
            return true;
        if(options.db_path.empty())
        {
            std::cerr << "--db is required\n";
            return false;
        }
        return true;
    }
    std::string keyDistributionName(lsmkv::bench::KeyDistribution distribution)
    {
        if(distribution == lsmkv::bench::KeyDistribution::kUniform)
            return "uniform";
        if(distribution == lsmkv::bench::KeyDistribution::kSequential)
            return "sequential";
        return "zipfian";
    }
    std::string syncModeName(lsmkv::SyncMode sync_mode)
    {
        if(sync_mode == lsmkv::SyncMode::kSyncEveryWrite)
            return "every-write";
        if(sync_mode == lsmkv::SyncMode::kSyncEveryN)
            return "every-n";
        return "off";
    }
    std::uint64_t liveDataSize(const std::unordered_map<std::string, std::string>& live_data)
    {
        std::uint64_t size = 0;
        for(const auto& [key, value] : live_data)
            size += key.size() + value.size();
        return size;
    }
    bool sstableSize(const std::filesystem::path& directory, std::uint64_t& size)
    {
        std::error_code error;
        std::filesystem::directory_iterator it(directory, error);
        std::filesystem::directory_iterator end;
        std::uint64_t total_size = 0;
        while(!error && it != end)
        {
            const std::filesystem::path path = it->path();
            it.increment(error);
            if(path.extension() != ".sst")
                continue;
            const std::uintmax_t file_size = std::filesystem::file_size(path, error);
            if(error || file_size > std::numeric_limits<std::uint64_t>::max() - total_size)
                return false;
            total_size += static_cast<std::uint64_t>(file_size);
        }
        if(error)
            return false;
        size = total_size;
        return true;
    }
}

int main(int argc, char* argv[])
{
    BenchOptions options;
    if(!parseArguments(argc, argv, options))
    {
        printUsage();
        return 1;
    }
    if(options.show_help)
    {
        printUsage();
        return 0;
    }
    if(options.warmup_operation_count > std::numeric_limits<std::uint64_t>::max() - options.workload.operation_count)
    {
        std::cerr << "operation count is too large\n";
        return 1;
    }
    lsmkv::bench::WorkloadConfig generator_config = options.workload;
    generator_config.operation_count += options.warmup_operation_count;
    lsmkv::bench::WorkloadGenerator generator(generator_config);
    if(!generator.isValid())
    {
        std::cerr << "invalid workload configuration\n";
        return 1;
    }
    const std::filesystem::path db_path(options.db_path);
    std::error_code error;
    if(std::filesystem::exists(db_path, error) || error)
    {
        std::cerr << "database path already exists or cannot be checked\n";
        return 1;
    }
    auto db = lsmkv::DB::open(db_path.string(), options.sync_mode, options.sync_interval);
    if(!db)
    {
        std::cerr << "failed to open database\n";
        return 1;
    }
    const std::string initial_value(options.workload.min_value_size, 'p');
    std::unordered_map<std::string, std::string> live_data;
    for(std::uint64_t i = 0; i < options.workload.key_count; i++)
    {
        const std::string key = lsmkv::bench::formatKey(i);
        if(!db->put(key, initial_value))
        {
            std::cerr << "failed during preload\n";
            return 1;
        }
        live_data[key] = initial_value;
    }
    if(!db->flush())
    {
        std::cerr << "failed to flush preload data\n";
        return 1;
    }
    lsmkv::bench::Operation operation;
    std::string value;
    for(std::uint64_t i = 0; i < options.warmup_operation_count; i++)
    {
        if(!generator.next(operation))
        {
            std::cerr << "failed to generate warmup operation\n";
            return 1;
        }
        if(operation.type == lsmkv::bench::OperationType::kRead)
            db->get(operation.key, value);
        else if(operation.type == lsmkv::bench::OperationType::kWrite)
        {
            if(!db->put(operation.key, operation.value))
            {
                std::cerr << "write operation failed during warmup\n";
                return 1;
            }
            live_data[operation.key] = operation.value;
        }
        else
        {
            if(!db->deleteKey(operation.key))
            {
                std::cerr << "delete operation failed during warmup\n";
                return 1;
            }
            live_data.erase(operation.key);
        }
    }
    if(!db->flush())
    {
        std::cerr << "failed to flush warmup data\n";
        return 1;
    }
    const lsmkv::DBStats stats_before = db->stats();
    std::uint64_t read_count = 0;
    std::uint64_t write_count = 0;
    std::uint64_t delete_count = 0;
    std::uint64_t elapsed_ns = 0;
    std::uint64_t cache_hit_count = 0;
    std::uint64_t cache_miss_count = 0;
    lsmkv::bench::LatencyHistogram read_latency;
    lsmkv::bench::LatencyHistogram write_latency;
    lsmkv::bench::LatencyHistogram delete_latency;
    for(std::uint64_t i = 0; i < options.workload.operation_count; i++)
    {
        if(!generator.next(operation))
        {
            std::cerr << "failed to generate benchmark operation\n";
            return 1;
        }
        if(operation.type == lsmkv::bench::OperationType::kRead)
        {
            const std::uint64_t hits_before = db->blockCacheHitCount();
            const std::uint64_t misses_before = db->blockCacheMissCount();
            const auto start = std::chrono::steady_clock::now();
            db->get(operation.key, value);
            const auto end = std::chrono::steady_clock::now();
            const std::uint64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            read_latency.record(latency_ns);
            elapsed_ns += latency_ns;
            cache_hit_count += db->blockCacheHitCount() - hits_before;
            cache_miss_count += db->blockCacheMissCount() - misses_before;
            read_count++;
        }
        else if(operation.type == lsmkv::bench::OperationType::kWrite)
        {
            const auto start = std::chrono::steady_clock::now();
            const bool success = db->put(operation.key, operation.value);
            const auto end = std::chrono::steady_clock::now();
            const std::uint64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            write_latency.record(latency_ns);
            elapsed_ns += latency_ns;
            if(!success)
            {
                std::cerr << "write operation failed\n";
                return 1;
            }
            live_data[operation.key] = operation.value;
            write_count++;
        }
        else
        {
            const auto start = std::chrono::steady_clock::now();
            const bool success = db->deleteKey(operation.key);
            const auto end = std::chrono::steady_clock::now();
            const std::uint64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            delete_latency.record(latency_ns);
            elapsed_ns += latency_ns;
            if(!success)
            {
                std::cerr << "delete operation failed\n";
                return 1;
            }
            live_data.erase(operation.key);
            delete_count++;
        }
    }
    if(!db->flush())
    {
        std::cerr << "failed to flush workload data\n";
        return 1;
    }
    const lsmkv::DBStats stats_after = db->stats();
    std::uint64_t sstable_size = 0;
    if(!sstableSize(db_path, sstable_size))
    {
        std::cerr << "failed to calculate SSTable size\n";
        return 1;
    }
    lsmkv::bench::BenchmarkResult result;
    result.seed = options.workload.seed;
    result.operation_count = options.workload.operation_count;
    result.warmup_operation_count = options.warmup_operation_count;
    result.key_count = options.workload.key_count;
    result.read_percent = options.workload.read_percent;
    result.write_percent = options.workload.write_percent;
    result.delete_percent = options.workload.delete_percent;
    result.key_distribution = keyDistributionName(options.workload.key_distribution);
    result.zipf_theta = options.workload.zipf_theta;
    result.min_value_size = options.workload.min_value_size;
    result.max_value_size = options.workload.max_value_size;
    result.sync_mode = syncModeName(options.sync_mode);
    result.sync_interval = options.sync_interval;
    result.read_count = read_count;
    result.write_count = write_count;
    result.delete_count = delete_count;
    result.elapsed_ns = elapsed_ns;
    if(elapsed_ns != 0)
        result.throughput_ops_per_second = static_cast<double>(options.workload.operation_count) * 1000000000.0 / static_cast<double>(elapsed_ns);
    result.read_latency = read_latency.summarize();
    result.write_latency = write_latency.summarize();
    result.delete_latency = delete_latency.summarize();
    result.wal_bytes_written = stats_after.wal_bytes_written - stats_before.wal_bytes_written;
    result.sstable_bytes_written = stats_after.sstable_bytes_written - stats_before.sstable_bytes_written;
    result.user_bytes_written = stats_after.user_bytes_written - stats_before.user_bytes_written;
    result.write_amplification = lsmkv::bench::calculateWriteAmplification(result.wal_bytes_written, result.sstable_bytes_written, result.user_bytes_written);
    result.live_data_bytes = liveDataSize(live_data);
    result.sstable_size_bytes = sstable_size;
    result.space_amplification = lsmkv::bench::calculateSpaceAmplification(result.sstable_size_bytes, result.live_data_bytes);
    result.cache_hit_count = cache_hit_count;
    result.cache_miss_count = cache_miss_count;
    if(cache_hit_count + cache_miss_count != 0)
        result.cache_hit_rate = static_cast<double>(cache_hit_count) / static_cast<double>(cache_hit_count + cache_miss_count);
    result.compaction_count = stats_after.compaction_count - stats_before.compaction_count;
    result.compaction_bytes_read = stats_after.compaction_bytes_read - stats_before.compaction_bytes_read;
    result.compaction_bytes_written = stats_after.compaction_bytes_written - stats_before.compaction_bytes_written;
    if(!options.output_path.empty())
    {
        const bool output_written = options.output_format == OutputFormat::kCsv ? lsmkv::bench::writeCsv(options.output_path, result) : lsmkv::bench::writeJson(options.output_path, result);
        if(!output_written)
        {
            std::cerr << "failed to write benchmark output\n";
            return 1;
        }
    }
    db->close();
    std::cout << "seed: " << result.seed << '\n';
    std::cout << "operations: " << result.operation_count << '\n';
    std::cout << "reads: " << read_count << '\n';
    std::cout << "writes: " << write_count << '\n';
    std::cout << "deletes: " << delete_count << '\n';
    std::cout << "throughput: " << result.throughput_ops_per_second << " ops/sec\n";
    std::cout << "read p99: " << result.read_latency.p99_ns << " ns\n";
    std::cout << "write amplification: " << result.write_amplification << '\n';
    std::cout << "space amplification: " << result.space_amplification << '\n';
    std::cout << "cache hit rate: " << result.cache_hit_rate << '\n';
    return 0;
}
