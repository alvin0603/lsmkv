#pragma once

#include <lsmkv/memtable.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lsmkv
{
    struct WalReplayResult
    {
        std::size_t records_replayed = 0;
        std::uint64_t max_sequence = 0;
    };
    class WalReader
    {
        public:
            explicit WalReader(std::string_view path);
            ~WalReader();
            WalReader(const WalReader&) = delete;
            WalReader& operator=(const WalReader&) = delete;
            bool isOpen() const;
            bool replay(MemTable& memtable, WalReplayResult& result);
        private:
            bool truncate(std::uint64_t size);
            int fd_ = -1;
    };
}
