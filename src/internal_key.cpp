#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>

namespace lsmkv
{
    bool appendInternalKey(std::string& output, std::string_view user_key, std::uint64_t sequence, ValueType type)
    {
        const auto type_value = static_cast<std::uint8_t>(type);
        if(sequence > kMaxSequenceNumber)
            return false;
        if(type_value > static_cast<std::uint8_t>(ValueType::kPut))
            return false;
        const std::uint64_t trailer = (sequence << 8) | type_value;
        output.append(user_key.data(), user_key.size());
        appendFixed64(output, trailer);
        return true;
    }
    bool parseInternalKey(std::string_view encode, ParsedInternalKey& result)
    {
        if(encode.size() < sizeof(std::uint64_t))
            return false;
        const std::size_t user_key_size = encode.size() - sizeof(std::uint64_t);
        std::string_view trailer_input = encode.substr(user_key_size);
        std::uint64_t trailer = 0;
        if(!consumeFixed64(trailer_input, trailer))
            return false;
        const auto type_value = static_cast<std::uint8_t>(trailer & 0xFF);
        if(type_value > static_cast<std::uint8_t>(ValueType::kPut))
            return false;
        ParsedInternalKey decode;
        decode.user_key = encode.substr(0, user_key_size);
        decode.sequence = trailer >> 8;
        decode.type = static_cast<ValueType>(type_value);
        result = decode;
        return true;
    }
    bool InternalKeyComparator::operator()(std::string_view a, std::string_view b) const
    {
        ParsedInternalKey left;
        ParsedInternalKey right;
        const bool left_valid = parseInternalKey(a, left);
        const bool right_valid = parseInternalKey(b, right);
        if(left_valid != right_valid)
            return !left_valid;
        if(!left_valid)
            return a < b;
        if(left.user_key < right.user_key)
            return true;
        if(left.user_key > right.user_key)
            return false;
        
        // If the user keys are the same
        const std::uint64_t left_trailer = (left.sequence << 8) | static_cast<std::uint8_t>(left.type);
        const std::uint64_t right_trailer = (right.sequence << 8) | static_cast<std::uint8_t>(right.type);
        return left_trailer > right_trailer;
    }
}