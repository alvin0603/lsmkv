#pragma once

#include <lsmkv/memtable.h>
#include <lsmkv/wal_writer.h>
#include <lsmkv/manifest.h>
#include <lsmkv/sstable_reader.h>
#include <lsmkv/db_iterator.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <string>
#include <vector>

namespace lsmkv
{
    inline constexpr std::size_t kDefaultMemTableSize = 4 * 1024 * 1024;

    class DB
    {
        public:
            static std::unique_ptr<DB> open(std::string_view path, SyncMode sync_mode = SyncMode::kSyncEveryWrite, std::size_t sync_interval = 1, std::size_t memtable_flush_size = kDefaultMemTableSize);
            ~DB();
            DB(const DB&) = delete;
            DB& operator=(const DB&) = delete;
            bool isOpen() const;
            bool put(std::string_view user_key, std::string_view value);
            LookupResult get(std::string_view user_key, std::string& value) const;
            bool deleteKey(std::string_view user_key);
            void close();
            bool flush();
            std::unique_ptr<DBIterator> newIterator() const;
        private:
            struct OpenedTable
            {
                TableMetaData metadata;
                std::unique_ptr<SSTableReader> reader;
            };
            DB() = default; // ensure that DB can only be created through the open() 
            MemTable memtable_;
            std::unique_ptr<MemTable> immutable_memtable_;
            std::unique_ptr<WalWriter> wal_writer_; // destroyed when DB is closed
            std::vector<OpenedTable> opened_tables_;
            ManifestState manifest_state_;
            std::string directory_;
            std::uint64_t next_sequence_ = 1;
            std::uint64_t active_wal_epoch_ = 1;
            SyncMode sync_mode_ = SyncMode::kSyncEveryWrite;
            std::size_t sync_interval_ = 1;
            std::size_t memtable_flush_size_ = kDefaultMemTableSize;
            int lock_fd_ = -1;
            bool open_ = false;
            friend class DBIterator;

            bool flushMemTable();
            bool handleFlushFailure();
            void sortOpenedTables();
    };
}
