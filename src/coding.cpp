#include <lsmkv/coding.h>

#include <cstddef>

namespace lsmkv
{
    void appendFixed32(std::string& output, std::uint32_t value)
    {
        for (std::size_t i = 0; i < sizeof(value); i++)
        {
            output.push_back(static_cast<char>(value & 0xFF));
            value >>= 8;
        }
    }
    void appendFixed64(std::string& output, std::uint64_t value)
    {
        for (std::size_t i = 0; i < sizeof(value); i++)
        {
            output.push_back(static_cast<char>(value & 0xFF));
            value >>= 8;
        }
    }
    bool consumeFixed32(std::string_view& input, std::uint32_t& value)
    {
        if (input.size() < sizeof(value))
            return false;
        std::uint32_t decode = 0;
        for (std::size_t i = 0; i < sizeof(decode); i++)
        {
            decode |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(input[i])) << 8*i;
        }
        value = decode;
        input.remove_prefix(sizeof(value));
        return true;
    }
    bool consumeFixed64(std::string_view& input, std::uint64_t& value)
    {
        if (input.size() < sizeof(value))
            return false;
        std::uint64_t decode = 0;
        for (std::size_t i = 0; i < sizeof(decode); i++)
        {
            decode |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(input[i])) << 8*i;
        }
        value = decode;
        input.remove_prefix(sizeof(value));
        return true;
    }
    void appendVarint32(std::string& output, std::uint32_t value)
    {
        while (value >= 0x80)
        {
            output.push_back(static_cast<char>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        output.push_back(static_cast<char>(value));
    }
    void appendVarint64(std::string& output, std::uint64_t value)
    {
        while (value >= 0x80)
        {
            output.push_back(static_cast<char>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        output.push_back(static_cast<char>(value));
    }
    bool consumeVarint32(std::string_view& input, std::uint32_t& value)
    {
        std::string_view remaining_input = input;
        std::uint32_t decode = 0;
        std::uint32_t shift = 0;
        while(!remaining_input.empty() && shift < 32)
        {
            std::uint8_t byte = static_cast<std::uint8_t>(remaining_input[0]);
            if (shift == 28 && byte > 0x0F)
                return false;
            remaining_input.remove_prefix(1);
            decode |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
            {
                input = remaining_input;
                value = decode;
                return true;
            }
            shift += 7;
        }
        return false;
    }
    bool consumeVarint64(std::string_view& input, std::uint64_t& value)
    {
        std::string_view remaining_input = input;
        std::uint64_t decode = 0;
        std::uint32_t shift = 0;
        while(!remaining_input.empty() && shift < 64)
        {
            std::uint8_t byte = static_cast<std::uint8_t>(remaining_input[0]);
            if (shift == 63 && byte > 0x01)
                return false;
            remaining_input.remove_prefix(1);
            decode |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0)
            {
                input = remaining_input;
                value = decode;
                return true;
            }
            shift += 7;
        }
        return false;
    }
    bool appendLengthPrefixedSlice(std::string& output, std::string_view value)
    {
        if(value.size() > UINT32_MAX)
            return false;
        appendVarint32(output, static_cast<std::uint32_t>(value.size()));
        output.append(value.data(), value.size());
        return true;
    }
    bool consumeLengthPrefixedSlice(std::string_view& input, std::string_view& value)
    {
        std::string_view remaining_input = input;
        std::uint32_t length = 0;
        if(!consumeVarint32(remaining_input, length))
            return false;
        if(remaining_input.size() < length)
            return false;
        
        std::string_view decode = remaining_input.substr(0, length);
        remaining_input.remove_prefix(length);
        input = remaining_input;
        value = decode;
        return true;
    }
    std::uint32_t calculateCrc32(std::string_view input)
    {
        std::uint32_t crc = 0xFFFFFFFF;
        for (char character : input)
        {
            crc ^= static_cast<std::uint8_t>(character);
            for (std::size_t i = 0; i < 8; i++)
            {
                if ((crc & 1) == 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc >>= 1;
            }
        }
        return crc ^ 0xFFFFFFFF;
    }
}
