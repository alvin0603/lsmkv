#pragma once

#include <lsmkv/memtable.h>
#include <lsmkv/wal_writer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace lsmkv
{
    class DB
    {
        public:
            static std::unique_ptr<DB> open(std::string_view path, SyncMode sync_mode = SyncMode::kSyncEveryWrite, std::size_t sync_interval = 1);
            ~DB();
            DB(const DB&) = delete;
            DB& operator=(const DB&) = delete;
            bool isOpen() const;
            bool put(std::string_view user_key, std::string_view value);
            LookupResult get(std::string_view user_key, std::string& value) const;
            bool deleteKey(std::string_view user_key);
            void close();
        private:
            DB() = default; // ensure that DB can only be created through the open() 
            MemTable memtable_;
            std::unique_ptr<WalWriter> wal_writer_; // destroyed when DB is closed
            std::uint64_t next_sequence_ = 1;
            int lock_fd_ = -1;
            bool open_ = false;
    };
}