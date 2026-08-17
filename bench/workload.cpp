#include "workload.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace lsmkv::bench
{
    std::string formatKey(std::uint64_t key_index)
    {
        const std::string digits = std::to_string(key_index);
        return "key" + std::string(20 - digits.size(), '0') + digits;
    }
    WorkloadGenerator::WorkloadGenerator(const WorkloadConfig& config)
    {
        config_ = config;
        operation_generator_.seed(config_.seed ^ 0x243F6A8885A308D3ULL);
        key_generator_.seed(config_.seed ^ 0x13198A2E03707344ULL);
        value_generator_.seed(config_.seed ^ 0xA4093822299F31D0ULL);
        valid_ = validateConfig();
        if(valid_ && config_.key_distribution == KeyDistribution::kZipfian)
            buildZipfCdf();
    }
    bool WorkloadGenerator::validateConfig() const
    {
        if(config_.operation_count == 0 || config_.key_count == 0)
            return false;
        if(config_.read_percent > 100 || config_.write_percent > 100 || config_.delete_percent > 100)
            return false;
        if(config_.read_percent + config_.write_percent + config_.delete_percent != 100)
            return false;
        if(config_.min_value_size > config_.max_value_size)
            return false;
        if(config_.max_value_size > std::numeric_limits<std::uint32_t>::max())
            return false;
        if(config_.key_count > std::numeric_limits<std::size_t>::max())
            return false;
        if(config_.key_distribution != KeyDistribution::kUniform && config_.key_distribution != KeyDistribution::kSequential && config_.key_distribution != KeyDistribution::kZipfian)
            return false;
        if(!std::isfinite(config_.zipf_theta) || config_.zipf_theta < 0.0)
            return false;
        return true;
    }
    void WorkloadGenerator::buildZipfCdf()
    {
        zipf_cdf_.resize(static_cast<std::size_t>(config_.key_count));
        long double total_weight = 0.0;
        for(std::size_t i = 0; i < zipf_cdf_.size(); i++)
        {
            const long double rank = static_cast<long double>(i) + 1.0;
            const long double weight = 1.0L / std::pow(rank, static_cast<long double>(config_.zipf_theta));
            zipf_cdf_[i] = static_cast<double>(weight);
            total_weight += weight;
        }
        long double cumulative_weight = 0.0;
        for(double& probability : zipf_cdf_)
        {
            cumulative_weight += probability;
            probability = static_cast<double>(cumulative_weight / total_weight);
        }
        zipf_cdf_.back() = 1.0;
    }
    bool WorkloadGenerator::isValid() const
    {
        return valid_;
    }
    OperationType WorkloadGenerator::selectOperation()
    {
        std::uniform_int_distribution<std::uint32_t> distribution(0, 99);
        const std::uint32_t selection = distribution(operation_generator_);
        if(selection < config_.read_percent)
            return OperationType::kRead;
        if(selection < config_.read_percent + config_.write_percent)
            return OperationType::kWrite;
        return OperationType::kDelete;
    }
    std::uint64_t WorkloadGenerator::selectKeyIndex()
    {
        if(config_.key_distribution == KeyDistribution::kSequential)
            return operation_index_ % config_.key_count;
        if(config_.key_distribution == KeyDistribution::kUniform)
        {
            std::uniform_int_distribution<std::uint64_t> distribution(0, config_.key_count - 1);
            return distribution(key_generator_);
        }
        std::uniform_real_distribution<double> distribution(0.0, 1.0);
        const double selection = distribution(key_generator_);
        const auto position = std::lower_bound(zipf_cdf_.begin(), zipf_cdf_.end(), selection);
        if(position == zipf_cdf_.end())
            return config_.key_count - 1;
        return static_cast<std::uint64_t>(std::distance(zipf_cdf_.begin(), position));
    }
    std::size_t WorkloadGenerator::selectValueSize()
    {
        if(config_.min_value_size == config_.max_value_size)
            return config_.min_value_size;
        std::uniform_int_distribution<std::size_t> distribution(config_.min_value_size, config_.max_value_size);
        return distribution(value_generator_);
    }
    bool WorkloadGenerator::next(Operation& operation)
    {
        if(!valid_ || operation_index_ >= config_.operation_count)
            return false;
        operation = Operation{};
        operation.type = selectOperation();
        operation.key = formatKey(selectKeyIndex());
        if(operation.type == OperationType::kWrite)
        {
            const std::size_t value_size = selectValueSize();
            const char value_byte = static_cast<char>('a' + operation_index_ % 26);
            operation.value.assign(value_size, value_byte);
        }
        operation_index_++;
        return true;
    }
}
