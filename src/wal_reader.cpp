#include <lsmkv/wal_reader.h>
#include <lsmkv/coding.h>

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace lsmkv
{
    constexpr std::uint32_t kWalHeaderSize = 8;
    constexpr std::uint32_t kMaxWalPayloadSize = 64 * 1024 * 1024;
    
    WalReader::WalReader(std::string_view path)
    {
        const std::string path_str(path);
        fd_ = ::open(path_str.c_str(), O_RDWR);
    }
    WalReader::~WalReader()
    {
        if (fd_ != -1)
            ::close(fd_);
    }
    bool WalReader::isOpen() const
    {
        return fd_ != -1;
    }
    WalReader::ReadResult WalReader::readExact(char* output, std::size_t size)
    {
        std::size_t bytes_read = 0;
        while(bytes_read < size)
        {
            ssize_t result = ::read(fd_, output + bytes_read, size - bytes_read);
            if (result == -1)
            {
                if (errno == EINTR)
                    continue;
                return ReadResult::kError;
            }
            else if (result == 0)
            {
                if(bytes_read == 0)
                    return ReadResult::kEnd;
                return ReadResult::kPartial;
            }
            bytes_read += static_cast<std::size_t>(result);
        }
        return ReadResult::kComplete;
    }
    bool WalReader::truncate(std::uint64_t size)
    {
        while(::ftruncate(fd_, static_cast<off_t>(size)) == -1)
        {
            if(errno == EINTR)
                continue;
            return false;
        }
        return true;
    }
    bool WalReader::replay(MemTable& memtable, WalReplayResult& result)
    {
        if(fd_ == -1)
            return false;
        result.records_replayed = 0;
        result.max_sequence = 0;
        std::uint64_t last_valid_offset = 0;
        while(true)
        {
            std::string header(kWalHeaderSize, '\0');
            const ReadResult header_result = readExact(header.data(), header.size());
            if(header_result == ReadResult::kEnd)
                return true;
            if(header_result == ReadResult::kError)
                return false;
            if(header_result == ReadResult::kPartial)
                return truncate(last_valid_offset);
            
            // parse CRC and length
            std::string_view header_input = header;
            std::uint32_t stored_crc = 0;
            std::uint32_t payload_length = 0;
            if(!consumeFixed32(header_input, stored_crc))
                return false;
            if(!consumeFixed32(header_input, payload_length))
                return false;
            if(payload_length > kMaxWalPayloadSize)
                return truncate(last_valid_offset);
            
            // read payload
            std::string payload(payload_length, '\0');
            const ReadResult payload_result = readExact(payload.data(), payload.size());
            if(payload_result == ReadResult::kError)
                return false;
            if(payload_result != ReadResult::kComplete)
                return truncate(last_valid_offset);
            if(calculateCrc32(payload) != stored_crc)
                return truncate(last_valid_offset);

            // parse type
            std::string_view payload_input = payload;
            if(payload_input.empty())
                return truncate(last_valid_offset);
            const std::uint8_t type_byte = static_cast<std::uint8_t>(payload_input[0]);
            payload_input.remove_prefix(1);
            if(type_byte != static_cast<std::uint8_t>(ValueType::kDelete) && type_byte != static_cast<std::uint8_t>(ValueType::kPut))
                return truncate(last_valid_offset);
            const ValueType type = static_cast<ValueType>(type_byte);

            // parse seq, key and value
            std::uint64_t sequence = 0;
            std::string_view user_key;
            std::string_view value;
            if(!consumeFixed64(payload_input, sequence))
                return truncate(last_valid_offset);
            if(sequence > kMaxSequenceNumber)
                return truncate(last_valid_offset);
            if(!consumeLengthPrefixedSlice(payload_input, user_key))
                return truncate(last_valid_offset);
            if(!consumeLengthPrefixedSlice(payload_input, value))
                return truncate(last_valid_offset);
            if(!payload_input.empty())
                return truncate(last_valid_offset);

            // add to MemTable
            if(!memtable.add(sequence, type, user_key, value))
                return false;
            result.records_replayed++;
            if(sequence > result.max_sequence)
                result.max_sequence = sequence;
            last_valid_offset += kWalHeaderSize + payload_length;
        }
    }
}