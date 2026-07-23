#include <catch_amalgamated.hpp>
#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>

#include <cstdint>
#include <string>

TEST_CASE("InternalKey stores user key and trailer", "[internal-key]")
{
    std::string encode;
    REQUIRE(lsmkv::appendInternalKey(encode, "name", 42, lsmkv::ValueType::kPut));
    REQUIRE(encode.size() == 12);
    REQUIRE(encode[0] == 'n');
    REQUIRE(encode[1] == 'a');
    REQUIRE(encode[2] == 'm');
    REQUIRE(encode[3] == 'e');
    REQUIRE(static_cast<std::uint8_t>(encode[4]) == 0x01);
    REQUIRE(static_cast<std::uint8_t>(encode[5]) == 0x2A);
    for (std::size_t i = 6; i < 12; i++)
        REQUIRE(static_cast<std::uint8_t>(encode[i]) == 0x00);
}

TEST_CASE("InternalKey survives an encode-decode round trip", "[internal-key]")
{
    std::string encode;
    REQUIRE(lsmkv::appendInternalKey(encode, "apple", 123456, lsmkv::ValueType::kPut));
    lsmkv::ParsedInternalKey parsed;
    REQUIRE(lsmkv::parseInternalKey(encode, parsed));
    REQUIRE(parsed.user_key == "apple");
    REQUIRE(parsed.sequence == 123456);
    REQUIRE(parsed.type == lsmkv::ValueType::kPut);
}

TEST_CASE("InternalKey supports deletion and maximum sequence", "[internal-key]")
{
    std::string encode;
    REQUIRE(lsmkv::appendInternalKey(encode, "deleted-key", lsmkv::kMaxSequenceNumber, lsmkv::ValueType::kDelete));
    lsmkv::ParsedInternalKey parsed;
    REQUIRE(lsmkv::parseInternalKey(encode, parsed));
    REQUIRE(parsed.user_key == "deleted-key");
    REQUIRE(parsed.sequence == lsmkv::kMaxSequenceNumber);
    REQUIRE(parsed.type == lsmkv::ValueType::kDelete);
}

TEST_CASE("InternalKey rejects sequence overflow", "[internal-key]")
{
    std::string output = "unchanged";
    REQUIRE_FALSE(lsmkv::appendInternalKey(output, "key", lsmkv::kMaxSequenceNumber + 1, lsmkv::ValueType::kPut));
    REQUIRE(output == "unchanged");
}

TEST_CASE("InternalKey rejects incomplete trailer", "[internal-key]")
{
    std::string_view encoded{"\x01\x02\x03", 3};
    lsmkv::ParsedInternalKey parsed{"unchanged", 99, lsmkv::ValueType::kPut};
    REQUIRE_FALSE(lsmkv::parseInternalKey(encoded, parsed));
    REQUIRE(parsed.user_key == "unchanged");
    REQUIRE(parsed.sequence == 99);
    REQUIRE(parsed.type == lsmkv::ValueType::kPut);
}

TEST_CASE("InternalKey rejects invalid value type", "[internal-key]")
{
    std::string encoded = "key";
    const std::uint64_t invalid_trailer = (42ULL << 8) | 0x02;
    lsmkv::appendFixed64(encoded, invalid_trailer);
    lsmkv::ParsedInternalKey parsed{"unchanged", 99, lsmkv::ValueType::kPut};
    REQUIRE_FALSE(lsmkv::parseInternalKey(encoded, parsed));
    REQUIRE(parsed.user_key == "unchanged");
    REQUIRE(parsed.sequence == 99);
    REQUIRE(parsed.type == lsmkv::ValueType::kPut);
}

TEST_CASE("InternalKeyComparator orders user keys ascending", "[internal-key]")
{
    std::string alvin;
    std::string huang;
    REQUIRE(lsmkv::appendInternalKey(alvin, "alvin", 10, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey( huang, "huang", 100, lsmkv::ValueType::kPut));
    lsmkv::InternalKeyComparator comparator;
    REQUIRE(comparator(alvin, huang));
    REQUIRE_FALSE(comparator(huang, alvin));
}

TEST_CASE("InternalKeyComparator orders newer versions first", "[internal-key]")
{
    std::string new_key;
    std::string old_key;
    REQUIRE(lsmkv::appendInternalKey(new_key, "alvin", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(old_key, "alvin", 10, lsmkv::ValueType::kPut));
    lsmkv::InternalKeyComparator comparator;
    REQUIRE(comparator(new_key, old_key));
    REQUIRE_FALSE(comparator(old_key, new_key));
}

TEST_CASE("InternalKeyComparator treats identical keys as equivalent", "[internal-key]")
{
    std::string key;
    REQUIRE(lsmkv::appendInternalKey(key, "alvin", 20, lsmkv::ValueType::kPut));
    lsmkv::InternalKeyComparator comparator;
    REQUIRE_FALSE(comparator(key, key));
}

TEST_CASE("InternalKeyComparator orders complete trailer descending", "[internal-key]")
{
    std::string put;
    std::string del;
    REQUIRE(lsmkv::appendInternalKey(put, "alvin", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(del, "alvin", 20, lsmkv::ValueType::kDelete));
    lsmkv::InternalKeyComparator comparator;
    REQUIRE(comparator(put, del));
    REQUIRE_FALSE(comparator(del, put));
}