#pragma once

#include <lsmkv/memtable.h>
#include <lsmkv/wal_writer.h>
#include <lsmkv/manifest.h>
#include <lsmkv/sstable_reader.h>
#include <lsmkv/db_iterator.h>
#include <lsmkv/block_cache.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <string>
#include <vector>

namespace lsmkv
{
    inline constexpr std::size_t kDefaultMemTableSize = 4 * 1024 * 1024;
    inline constexpr std::size_t kDefaultL0CompactionTrigger = 4;

    class CompactionPolicy;
    struct DBStats
    {
        std::uint64_t wal_bytes_written = 0;
        std::uint64_t sstable_bytes_written = 0;
        std::uint64_t user_bytes_written = 0;
        std::uint64_t compaction_count = 0;
        std::uint64_t compaction_bytes_read = 0;
        std::uint64_t compaction_bytes_written = 0;
    };
    class DB
    {
        public:
            static std::unique_ptr<DB> open(std::string_view path, SyncMode sync_mode = SyncMode::kSyncEveryWrite, std::size_t sync_interval = 1, std::size_t memtable_flush_size = kDefaultMemTableSize, std::size_t l0_compaction_trigger = kDefaultL0CompactionTrigger, std::size_t block_cache_capacity = kDefaultBlockCacheCapacity, std::size_t bloom_bits_per_key = kDefaultBloomBitsPerKey, std::uint32_t bloom_num_hashes = kDefaultBloomHashCount);
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
            std::uint64_t blockCacheHitCount() const;
            std::uint64_t blockCacheMissCount() const;
            double blockCacheHitRate() const;
            std::size_t blockCacheCapacity() const;
            void setBlockCacheCapacity(std::size_t capacity);
            std::size_t bloomMemoryUsage() const;
            DBStats stats() const;
        private:
            struct OpenedTable
            {
                TableMetaData metadata;
                std::unique_ptr<SSTableReader> reader;
            };
            DB(); // ensure that DB can only be created through the open()
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
            std::size_t l0_compaction_trigger_ = kDefaultL0CompactionTrigger;
            std::shared_ptr<BlockCache> block_cache_;
            std::unique_ptr<CompactionPolicy> compaction_policy_;
            std::size_t bloom_bits_per_key_ = kDefaultBloomBitsPerKey;
            std::uint32_t bloom_num_hashes_ = kDefaultBloomHashCount;
            DBStats stats_;
            int lock_fd_ = -1;
            bool open_ = false;
            friend class DBIterator;

            bool flushMemTable();
            bool handleFlushFailure();
            void sortOpenedTables();
            bool compact();
    };
}
