#include <lsmkv/wal_writer.h>
#include <lsmkv/coding.h>
#include "internal/file_io.h"

#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace lsmkv
{
    constexpr std::uint32_t kMaxWalPayloadSize = 64 * 1024 * 1024;

    WalWriter::WalWriter(std::string_view path, SyncMode sync_mode, std::size_t sync_interval)
    {
        sync_mode_ = sync_mode;
        sync_interval_ = sync_interval;
        if(sync_mode_ == SyncMode::kSyncEveryN && sync_interval_ == 0)
            return;
        const std::string path_string(path);
        fd_ = ::open(path_string.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    WalWriter::~WalWriter()
    {
        if(fd_ != -1)
            ::close(fd_);
    }
    bool WalWriter::isOpen() const
    {
        return fd_ != -1;
    }
    bool WalWriter::append(std::uint64_t sequence, ValueType type, std::string_view user_key, std::string_view value)
    {
        if(fd_ == -1)
            return false;
        if(sequence > kMaxSequenceNumber)
            return false;
        std::string payload;
        payload.push_back(static_cast<char>(type));
        appendFixed64(payload, sequence);
        if(!appendLengthPrefixedSlice(payload, user_key))
            return false;
        if(!appendLengthPrefixedSlice(payload, value))
            return false;
        if(payload.size() > kMaxWalPayloadSize)
            return false;
        std::string record;
        appendFixed32(record, calculateCrc32(payload));
        appendFixed32(record, static_cast<std::uint32_t>(payload.size()));
        record.append(payload);
        if(!writeAll(fd_, record))
            return false;
        bytes_written_ += record.size();
        writes_since_last_sync_++;
        if(sync_mode_ == SyncMode::kSyncEveryWrite)
            return sync();
        if(sync_mode_ == SyncMode::kSyncEveryN && writes_since_last_sync_ >= sync_interval_)
            return sync();
        return true;
    }
    bool WalWriter::sync()
    {
        if(fd_ == -1)
            return false;
        if(!syncFile(fd_))
            return false;
        writes_since_last_sync_ = 0;
        return true;
    }
    std::uint64_t WalWriter::bytesWritten() const
    {
        return bytes_written_;
    }
}


