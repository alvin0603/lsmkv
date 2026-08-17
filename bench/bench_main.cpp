#include "workload.h"
#include <lsmkv/db.h>

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    struct BenchOptions
    {
        lsmkv::bench::WorkloadConfig workload;
        lsmkv::SyncMode sync_mode = lsmkv::SyncMode::kSyncOff;
        std::size_t sync_interval = 1;
        std::string db_path;
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
    lsmkv::bench::WorkloadGenerator generator(options.workload);
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
    for(std::uint64_t i = 0; i < options.workload.key_count; i++)
    {
        if(!db->put(lsmkv::bench::formatKey(i), initial_value))
        {
            std::cerr << "failed during preload\n";
            return 1;
        }
    }
    if(!db->flush())
    {
        std::cerr << "failed to flush preload data\n";
        return 1;
    }
    std::uint64_t read_count = 0;
    std::uint64_t write_count = 0;
    std::uint64_t delete_count = 0;
    lsmkv::bench::Operation operation;
    std::string value;
    while(generator.next(operation))
    {
        if(operation.type == lsmkv::bench::OperationType::kRead)
        {
            db->get(operation.key, value);
            read_count++;
        }
        else if(operation.type == lsmkv::bench::OperationType::kWrite)
        {
            if(!db->put(operation.key, operation.value))
            {
                std::cerr << "write operation failed\n";
                return 1;
            }
            write_count++;
        }
        else
        {
            if(!db->deleteKey(operation.key))
            {
                std::cerr << "delete operation failed\n";
                return 1;
            }
            delete_count++;
        }
    }
    if(!db->flush())
    {
        std::cerr << "failed to flush workload data\n";
        return 1;
    }
    db->close();
    std::cout << "seed: " << options.workload.seed << '\n';
    std::cout << "operations: " << options.workload.operation_count << '\n';
    std::cout << "reads: " << read_count << '\n';
    std::cout << "writes: " << write_count << '\n';
    std::cout << "deletes: " << delete_count << '\n';
    return 0;
}
