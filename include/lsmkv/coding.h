#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lsmkv
{
    void appendFixed32(std::string& output, std::uint32_t value);
    void appendFixed64(std::string& output, std::uint64_t value);
    bool consumeFixed32(std::string_view& input, std::uint32_t& value);
    bool consumeFixed64(std::string_view& input, std::uint64_t& value);
    void appendVarint32(std::string& output, std::uint32_t value);
    void appendVarint64(std::string& output, std::uint64_t value);
    bool consumeVarint32(std::string_view& input, std::uint32_t& value);
    bool consumeVarint64(std::string_view& input, std::uint64_t& value);
    bool appendLengthPrefixedSlice(std::string& output, std::string_view value);
    bool consumeLengthPrefixedSlice(std::string_view& input, std::string_view& value);
    std::uint32_t calculateCrc32(std::string_view input);
}