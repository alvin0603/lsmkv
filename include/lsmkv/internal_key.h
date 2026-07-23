#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lsmkv
{
    enum class ValueType : std::uint8_t
    {
        kDelete = 0,
        kPut = 1,
    };
    struct ParsedInternalKey
    {
        std::string_view user_key;
        std::uint64_t sequence;
        ValueType type;
    };
    struct InternalKeyComparator
    {
        bool operator()(std::string_view a, std::string_view b) const;
    };
    inline constexpr std::uint64_t kMaxSequenceNumber = UINT64_MAX >> 8;
    bool appendInternalKey(std::string& output, std::string_view user_key, std::uint64_t sequence, ValueType type);
    bool parseInternalKey(std::string_view encode, ParsedInternalKey& result);
}