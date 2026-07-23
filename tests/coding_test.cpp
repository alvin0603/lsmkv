#include <catch_amalgamated.hpp>
#include <lsmkv/coding.h>

#include <cstdint>
#include <string>
#include <string_view>

TEST_CASE("Fixed32 uses little-endian byte order", "[coding]")
{
    std::string output;
    lsmkv::appendFixed32(output, 0x01020304);
    REQUIRE(output.size() == 4);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0x04);
    REQUIRE(static_cast<std::uint8_t>(output[1]) == 0x03);
    REQUIRE(static_cast<std::uint8_t>(output[2]) == 0x02);
    REQUIRE(static_cast<std::uint8_t>(output[3]) == 0x01);
}

TEST_CASE("Fixed64 uses little-endian byte order", "[coding]")
{
    std::string output;
    lsmkv::appendFixed64(output, 0x0102030405060708);
    REQUIRE(output.size() == 8);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0x08);
    REQUIRE(static_cast<std::uint8_t>(output[1]) == 0x07);
    REQUIRE(static_cast<std::uint8_t>(output[2]) == 0x06);
    REQUIRE(static_cast<std::uint8_t>(output[3]) == 0x05);
    REQUIRE(static_cast<std::uint8_t>(output[4]) == 0x04);
    REQUIRE(static_cast<std::uint8_t>(output[5]) == 0x03);
    REQUIRE(static_cast<std::uint8_t>(output[6]) == 0x02);
    REQUIRE(static_cast<std::uint8_t>(output[7]) == 0x01);
}

TEST_CASE("Fixed-width encoding appends bytes to existing output", "[coding]")
{
    std::string output = "X";
    lsmkv::appendFixed32(output, 0x01020304);
    lsmkv::appendFixed64(output, UINT64_MAX);
    REQUIRE(output.size() == 13);
    REQUIRE(output[0] == 'X');
    REQUIRE(static_cast<std::uint8_t>(output[1]) == 0x04);
    REQUIRE(static_cast<std::uint8_t>(output[2]) == 0x03);
    REQUIRE(static_cast<std::uint8_t>(output[3]) == 0x02);
    REQUIRE(static_cast<std::uint8_t>(output[4]) == 0x01);
    REQUIRE(static_cast<std::uint8_t>(output[5]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[6]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[7]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[8]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[9]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[10]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[11]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[12]) == 0xFF);
}

TEST_CASE("Fixed-width integers survive an encode-decode round trip", "[coding]")
{
    std::string encode;
    lsmkv::appendFixed32(encode, 0x01020304);
    lsmkv::appendFixed64(encode, 0x05060708090A0B0C);
    std::string_view input = encode;
    std::uint32_t decode_32 = 0;
    std::uint64_t decode_64 = 0;
    REQUIRE(lsmkv::consumeFixed32(input, decode_32));
    REQUIRE(decode_32 == 0x01020304);
    REQUIRE(lsmkv::consumeFixed64(input, decode_64));
    REQUIRE(decode_64 == 0x05060708090A0B0C);
    REQUIRE(input.empty());
}

TEST_CASE("Fixed-width decoding rejects incomplete input", "[coding]")
{
    std::string_view input{"\x01\x02\x03", 3};
    std::uint32_t value = 99;
    REQUIRE_FALSE(lsmkv::consumeFixed32(input, value));
    REQUIRE(input.size() == 3);
    REQUIRE(value == 99);
}

TEST_CASE("Varint32 uses the minimum required bytes", "[coding]")
{
    std::string output;
    lsmkv::appendVarint32(output, 0x01);
    REQUIRE(output.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0x01);

    output.clear();
    lsmkv::appendVarint32(output, 0x7F);
    REQUIRE(output.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0x7F);

    output.clear();
    lsmkv::appendVarint32(output, 0x80);
    REQUIRE(output.size() == 2);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0x80);
    REQUIRE(static_cast<std::uint8_t>(output[1]) == 0x01);
}

TEST_CASE("Varint encoding handles maximum integer values", "[coding]")
{
    std::string output;
    lsmkv::appendVarint32(output, UINT32_MAX);
    REQUIRE(output.size() == 5);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[1]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[2]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[3]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[4]) == 0x0F);

    output.clear();
    lsmkv::appendVarint64(output, UINT64_MAX);
    REQUIRE(output.size() == 10);
    for (std::size_t i = 0; i < 9; i++)
        REQUIRE(static_cast<std::uint8_t>(output[i]) == 0xFF);
    REQUIRE(static_cast<std::uint8_t>(output[9]) == 0x01);
}

TEST_CASE("Varint integers survive an encode-decode round trip", "[coding]")
{
    std::string encode;
    lsmkv::appendVarint32(encode, 0);
    lsmkv::appendVarint32(encode, 127);
    lsmkv::appendVarint32(encode, 128);
    lsmkv::appendVarint32(encode, 300);
    lsmkv::appendVarint32(encode, UINT32_MAX);
    lsmkv::appendVarint64(encode, UINT64_MAX);
    std::string_view input = encode;
    std::uint32_t value32 = 0;
    std::uint64_t value64 = 0;
    REQUIRE(lsmkv::consumeVarint32(input, value32));
    REQUIRE(value32 == 0);
    REQUIRE(lsmkv::consumeVarint32(input, value32));
    REQUIRE(value32 == 127);
    REQUIRE(lsmkv::consumeVarint32(input, value32));
    REQUIRE(value32 == 128);
    REQUIRE(lsmkv::consumeVarint32(input, value32));
    REQUIRE(value32 == 300);
    REQUIRE(lsmkv::consumeVarint32(input, value32));
    REQUIRE(value32 == UINT32_MAX);
    REQUIRE(lsmkv::consumeVarint64(input, value64));
    REQUIRE(value64 == UINT64_MAX);
    REQUIRE(input.empty());
}
TEST_CASE("Varint decoding rejects incomplete input", "[coding]")
{
    std::string_view input{"\x80", 1};
    std::uint32_t value = 99;
    REQUIRE_FALSE(lsmkv::consumeVarint32(input, value));
    REQUIRE(input.size() == 1);
    REQUIRE(value == 99);
}
TEST_CASE("Varint decoding rejects integer overflow", "[coding]")
{
    std::string_view input32{"\xFF\xFF\xFF\xFF\x10", 5};
    std::uint32_t value32 = 99;
    REQUIRE_FALSE(lsmkv::consumeVarint32(input32, value32));
    REQUIRE(input32.size() == 5);
    REQUIRE(value32 == 99);
    std::string_view input64{"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x02", 10};
    std::uint64_t value64 = 99;
    REQUIRE_FALSE(lsmkv::consumeVarint64(input64, value64));
    REQUIRE(input64.size() == 10);
    REQUIRE(value64 == 99);
}

TEST_CASE("Length-prefixed slice stores length before bytes", "[coding]")
{
    std::string output;
    REQUIRE(lsmkv::appendLengthPrefixedSlice(output, "alvin"));
    REQUIRE(output.size() == 6);
    REQUIRE(static_cast<std::uint8_t>(output[0]) == 0x05);
    REQUIRE(output[1] == 'a');
    REQUIRE(output[2] == 'l');
    REQUIRE(output[3] == 'v');
    REQUIRE(output[4] == 'i');
    REQUIRE(output[5] == 'n');
}

TEST_CASE("Length-prefixed slices survive an encode-decode round trip", "[coding]")
{
    std::string encode;
    REQUIRE(lsmkv::appendLengthPrefixedSlice(encode, "alvin"));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(encode, "is"));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(encode, "me"));
    std::string_view input = encode;
    std::string_view value;
    REQUIRE(lsmkv::consumeLengthPrefixedSlice(input, value));
    REQUIRE(value == "alvin");
    REQUIRE(lsmkv::consumeLengthPrefixedSlice(input, value));
    REQUIRE(value == "is");
    REQUIRE(lsmkv::consumeLengthPrefixedSlice(input, value));
    REQUIRE(value == "me");
    REQUIRE(input.empty());
}

TEST_CASE("Length-prefixed slice preserves zero bytes", "[coding]")
{
    const std::string original{"A\0B", 3};
    std::string encoded;
    REQUIRE(lsmkv::appendLengthPrefixedSlice(encoded, original));
    std::string_view input = encoded;
    std::string_view decoded;
    REQUIRE(lsmkv::consumeLengthPrefixedSlice(input, decoded));
    REQUIRE(decoded.size() == 3);
    REQUIRE(decoded[0] == 'A');
    REQUIRE(decoded[1] == '\0');
    REQUIRE(decoded[2] == 'B');
    REQUIRE(input.empty());
}

TEST_CASE("Length-prefixed slice rejects incomplete content", "[coding]")
{
    std::string_view input{"\x05" "abc", 4};
    std::string_view value = "unchanged";
    REQUIRE_FALSE(lsmkv::consumeLengthPrefixedSlice(input, value));
    REQUIRE(input.size() == 4);
    REQUIRE(value == "unchanged");
}

TEST_CASE("CRC32 matches standard check values", "[coding]")
{
    REQUIRE(lsmkv::calculateCrc32("") == 0x00000000U);
    REQUIRE(lsmkv::calculateCrc32("123456789") == 0xCBF43926U);
    REQUIRE(lsmkv::calculateCrc32("hello") == 0x3610A686U);
}

TEST_CASE("CRC32 changes when input data changes", "[coding]")
{
    const std::string original = "hello";
    std::string changed = original;
    changed[0] ^= 0x01;
    REQUIRE(original != changed);
    REQUIRE(lsmkv::calculateCrc32(original) != lsmkv::calculateCrc32(changed));
}