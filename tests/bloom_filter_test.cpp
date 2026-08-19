#include <catch_amalgamated.hpp>
#include <lsmkv/bloom_filter.h>
#include <lsmkv/coding.h>

#include <cstddef>
#include <cstdint>
#include <string>

TEST_CASE("BloomFilter has no false negatives", "[bloom-filter]")
{
    lsmkv::BloomFilter filter;
    REQUIRE(filter.mayContain("apple"));
    REQUIRE(filter.add("apple"));
    REQUIRE(filter.add("banana"));
    REQUIRE(filter.add("cat"));
    REQUIRE(filter.finish());
    REQUIRE(filter.mayContain("apple"));
    REQUIRE(filter.mayContain("banana"));
    REQUIRE(filter.mayContain("cat"));
    REQUIRE_FALSE(filter.add("dog"));
    REQUIRE_FALSE(filter.finish());
}

TEST_CASE("BloomFilter reports its bit array memory", "[bloom-filter]")
{
    lsmkv::BloomFilter filter(10, 7);
    for(int i = 0; i < 10; i++)
        REQUIRE(filter.add("key" + std::to_string(i)));
    REQUIRE(filter.finish());
    REQUIRE(filter.memoryUsage() == 13);
    std::string encoded;
    REQUIRE(filter.encode(encoded));
    lsmkv::BloomFilter decoded;
    REQUIRE(decoded.decode(encoded));
    REQUIRE(decoded.memoryUsage() == 13);
}

TEST_CASE("BloomFilter encodes and decodes its complete state", "[bloom-filter]")
{
    lsmkv::BloomFilter filter;
    REQUIRE(filter.add("apple"));
    REQUIRE(filter.add("banana"));
    REQUIRE(filter.add("cat"));
    REQUIRE(filter.finish());
    std::string encoded;
    REQUIRE(filter.encode(encoded));
    lsmkv::BloomFilter decoded_filter;
    REQUIRE(decoded_filter.decode(encoded));
    REQUIRE(decoded_filter.mayContain("apple"));
    REQUIRE(decoded_filter.mayContain("banana"));
    REQUIRE(decoded_filter.mayContain("cat"));
    std::string truncated = encoded.substr(0, encoded.size() - 1);
    REQUIRE_FALSE(decoded_filter.decode(truncated));
    std::string trailing = encoded;
    trailing.push_back('\0');
    REQUIRE_FALSE(decoded_filter.decode(trailing));
    std::string invalid_hash_count;
    lsmkv::appendFixed32(invalid_hash_count, lsmkv::kMaxBloomHashCount + 1);
    lsmkv::appendFixed64(invalid_hash_count, 0);
    REQUIRE_FALSE(decoded_filter.decode(invalid_hash_count));
    lsmkv::BloomFilter empty_filter;
    REQUIRE(empty_filter.finish());
    REQUIRE_FALSE(empty_filter.mayContain("apple"));
}

TEST_CASE("BloomFilter keeps false positives below the expected limit", "[bloom-filter]")
{
    constexpr std::size_t key_count = 1000;
    constexpr std::size_t query_count = 10000;
    lsmkv::BloomFilter filter;
    for(std::size_t i = 0; i < key_count; i++)
        REQUIRE(filter.add("present-" + std::to_string(i)));
    REQUIRE(filter.finish());
    for(std::size_t i = 0; i < key_count; i++)
        REQUIRE(filter.mayContain("present-" + std::to_string(i)));
    std::size_t false_positives = 0;
    for(std::size_t i = 0; i < query_count; i++)
    {
        if(filter.mayContain("missing-" + std::to_string(i)))
            false_positives++;
    }
    REQUIRE(false_positives < query_count * 3 / 100);
}
