#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace lsmkv::bench
{
    enum class OperationType
    {
        kRead,
        kWrite,
        kDelete
    };
    enum class KeyDistribution
    {
        kUniform,
        kSequential,
        kZipfian
    };
    struct WorkloadConfig
    {
        std::uint64_t operation_count = 100000;
        std::uint64_t key_count = 10000;
        std::uint32_t read_percent = 80;
        std::uint32_t write_percent = 15;
        std::uint32_t delete_percent = 5;
        KeyDistribution key_distribution = KeyDistribution::kUniform;
        double zipf_theta = 0.99;
        std::size_t min_value_size = 100;
        std::size_t max_value_size = 100;
        std::uint64_t seed = 1;
    };
    struct Operation
    {
        OperationType type;
        std::string key;
        std::string value;
    };
    std::string formatKey(std::uint64_t key_index);
    class WorkloadGenerator
    {
        public:
            explicit WorkloadGenerator(const WorkloadConfig& config);
            bool isValid() const;
            bool next(Operation& operation);
        private:
            bool validateConfig() const;
            void buildZipfCdf();
            OperationType selectOperation();
            std::uint64_t selectKeyIndex();
            std::size_t selectValueSize();
            WorkloadConfig config_;
            std::uint64_t operation_index_ = 0;
            std::mt19937_64 operation_generator_;
            std::mt19937_64 key_generator_;
            std::mt19937_64 value_generator_;
            std::vector<double> zipf_cdf_;
            bool valid_ = false;
    };
}
