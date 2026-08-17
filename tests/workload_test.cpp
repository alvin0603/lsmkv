#include <catch_amalgamated.hpp>
#include <workload.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

TEST_CASE("WorkloadGenerator reproduces operations from the same seed", "[workload]")
{
    lsmkv::bench::WorkloadConfig config;
    config.operation_count = 1000;
    config.key_count = 100;
    config.key_distribution = lsmkv::bench::KeyDistribution::kZipfian;
    config.min_value_size = 4;
    config.max_value_size = 20;
    config.seed = 42;
    lsmkv::bench::WorkloadGenerator first_generator(config);
    lsmkv::bench::WorkloadGenerator second_generator(config);
    REQUIRE(first_generator.isValid());
    REQUIRE(second_generator.isValid());
    lsmkv::bench::Operation first_operation;
    lsmkv::bench::Operation second_operation;
    for(std::uint64_t i = 0; i < config.operation_count; i++)
    {
        REQUIRE(first_generator.next(first_operation));
        REQUIRE(second_generator.next(second_operation));
        REQUIRE(first_operation.type == second_operation.type);
        REQUIRE(first_operation.key == second_operation.key);
        REQUIRE(first_operation.value == second_operation.value);
    }
    REQUIRE_FALSE(first_generator.next(first_operation));
    REQUIRE_FALSE(second_generator.next(second_operation));
}

TEST_CASE("WorkloadGenerator changes its sequence with a different seed", "[workload]")
{
    lsmkv::bench::WorkloadConfig first_config;
    first_config.operation_count = 100;
    first_config.key_count = 100;
    lsmkv::bench::WorkloadConfig second_config = first_config;
    second_config.seed++;
    lsmkv::bench::WorkloadGenerator first_generator(first_config);
    lsmkv::bench::WorkloadGenerator second_generator(second_config);
    lsmkv::bench::Operation first_operation;
    lsmkv::bench::Operation second_operation;
    bool has_difference = false;
    for(std::uint64_t i = 0; i < first_config.operation_count; i++)
    {
        REQUIRE(first_generator.next(first_operation));
        REQUIRE(second_generator.next(second_operation));
        if(first_operation.type != second_operation.type || first_operation.key != second_operation.key || first_operation.value != second_operation.value)
            has_difference = true;
    }
    REQUIRE(has_difference);
}

TEST_CASE("WorkloadGenerator produces sequential keys", "[workload]")
{
    lsmkv::bench::WorkloadConfig config;
    config.operation_count = 5;
    config.key_count = 3;
    config.read_percent = 0;
    config.write_percent = 100;
    config.delete_percent = 0;
    config.key_distribution = lsmkv::bench::KeyDistribution::kSequential;
    config.min_value_size = 8;
    config.max_value_size = 8;
    lsmkv::bench::WorkloadGenerator generator(config);
    REQUIRE(generator.isValid());
    lsmkv::bench::Operation operation;
    for(std::uint64_t i = 0; i < config.operation_count; i++)
    {
        REQUIRE(generator.next(operation));
        REQUIRE(operation.type == lsmkv::bench::OperationType::kWrite);
        REQUIRE(operation.key == lsmkv::bench::formatKey(i % config.key_count));
        REQUIRE(operation.value.size() == 8);
    }
}

TEST_CASE("WorkloadGenerator keeps uniform keys inside the configured universe", "[workload]")
{
    lsmkv::bench::WorkloadConfig config;
    config.operation_count = 1000;
    config.key_count = 100;
    config.read_percent = 100;
    config.write_percent = 0;
    config.delete_percent = 0;
    config.key_distribution = lsmkv::bench::KeyDistribution::kUniform;
    lsmkv::bench::WorkloadGenerator generator(config);
    REQUIRE(generator.isValid());
    std::unordered_set<std::string> valid_keys;
    for(std::uint64_t i = 0; i < config.key_count; i++)
        valid_keys.insert(lsmkv::bench::formatKey(i));
    std::unordered_set<std::string> selected_keys;
    lsmkv::bench::Operation operation;
    while(generator.next(operation))
    {
        REQUIRE(valid_keys.contains(operation.key));
        selected_keys.insert(operation.key);
    }
    REQUIRE(selected_keys.size() > 80);
}

TEST_CASE("WorkloadGenerator creates a concentrated Zipfian distribution", "[workload]")
{
    lsmkv::bench::WorkloadConfig config;
    config.operation_count = 50000;
    config.key_count = 1000;
    config.read_percent = 100;
    config.write_percent = 0;
    config.delete_percent = 0;
    config.key_distribution = lsmkv::bench::KeyDistribution::kZipfian;
    config.zipf_theta = 1.2;
    lsmkv::bench::WorkloadGenerator generator(config);
    REQUIRE(generator.isValid());
    std::unordered_set<std::string> hot_keys;
    for(std::uint64_t i = 0; i < 10; i++)
        hot_keys.insert(lsmkv::bench::formatKey(i));
    std::uint64_t hot_count = 0;
    lsmkv::bench::Operation operation;
    while(generator.next(operation))
    {
        if(hot_keys.contains(operation.key))
            hot_count++;
    }
    REQUIRE(hot_count > config.operation_count * 40 / 100);
}

TEST_CASE("WorkloadGenerator follows operation ratios and value bounds", "[workload]")
{
    lsmkv::bench::WorkloadConfig config;
    config.operation_count = 100000;
    config.key_count = 100;
    config.min_value_size = 4;
    config.max_value_size = 12;
    config.seed = 81;
    lsmkv::bench::WorkloadGenerator generator(config);
    REQUIRE(generator.isValid());
    std::uint64_t read_count = 0;
    std::uint64_t write_count = 0;
    std::uint64_t delete_count = 0;
    bool has_different_value_sizes = false;
    std::size_t first_value_size = 0;
    lsmkv::bench::Operation operation;
    while(generator.next(operation))
    {
        if(operation.type == lsmkv::bench::OperationType::kRead)
        {
            REQUIRE(operation.value.empty());
            read_count++;
        }
        else if(operation.type == lsmkv::bench::OperationType::kWrite)
        {
            REQUIRE(operation.value.size() >= config.min_value_size);
            REQUIRE(operation.value.size() <= config.max_value_size);
            if(first_value_size == 0)
                first_value_size = operation.value.size();
            else if(first_value_size != operation.value.size())
                has_different_value_sizes = true;
            write_count++;
        }
        else
        {
            REQUIRE(operation.value.empty());
            delete_count++;
        }
    }
    REQUIRE(read_count > 79000);
    REQUIRE(read_count < 81000);
    REQUIRE(write_count > 14000);
    REQUIRE(write_count < 16000);
    REQUIRE(delete_count > 4000);
    REQUIRE(delete_count < 6000);
    REQUIRE(read_count + write_count + delete_count == config.operation_count);
    REQUIRE(has_different_value_sizes);
}

TEST_CASE("WorkloadGenerator rejects invalid configurations", "[workload]")
{
    lsmkv::bench::WorkloadConfig config;
    SECTION("zero operations")
    {
        config.operation_count = 0;
    }
    SECTION("zero keys")
    {
        config.key_count = 0;
    }
    SECTION("invalid operation ratio")
    {
        config.read_percent = 50;
    }
    SECTION("invalid value range")
    {
        config.min_value_size = 101;
        config.max_value_size = 100;
    }
    SECTION("invalid Zipfian theta")
    {
        config.zipf_theta = -0.1;
    }
    SECTION("invalid key distribution")
    {
        config.key_distribution = static_cast<lsmkv::bench::KeyDistribution>(99);
    }
    lsmkv::bench::WorkloadGenerator generator(config);
    REQUIRE_FALSE(generator.isValid());
    lsmkv::bench::Operation operation;
    REQUIRE_FALSE(generator.next(operation));
}

TEST_CASE("WorkloadGenerator formats numeric keys in lexical order", "[workload]")
{
    REQUIRE(lsmkv::bench::formatKey(2) < lsmkv::bench::formatKey(10));
    REQUIRE(lsmkv::bench::formatKey(10).size() == 23);
}
