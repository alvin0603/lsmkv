#pragma once

#include <lsmkv/internal_key.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lsmkv
{
    enum class SyncMode
    {
        kSyncEveryWrite,
        kSyncEveryN,
        kSyncOff
    };
    class WalWriter
    {
        public:
            WalWriter(std::string_view path, SyncMode sync_mode, std::size_t sync_interval = 1);
            ~WalWriter();
            // Deny copy and assignment to ensure the file descriptor is not closed multiple times
            WalWriter(const WalWriter&) = delete;
            WalWriter& operator=(const WalWriter&) = delete;
            bool isOpen() const;
            bool append(std::uint64_t sequence, ValueType type, std::string_view user_key, std::string_view value);
            bool sync();
        private:
            int fd_ = -1;
            SyncMode sync_mode_;
            std::size_t sync_interval_;
            std::size_t writes_since_last_sync_ = 0;
    };
}
