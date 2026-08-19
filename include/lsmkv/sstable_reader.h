#pragma once

#include <lsmkv/memtable.h>
#include <lsmkv/bloom_filter.h>
#include <lsmkv/block_cache.h>

#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace lsmkv
{
    class SSTableIterator;
    class SSTableReader
    {
        public:
            explicit SSTableReader(std::string_view path, std::uint64_t file_id = 0, std::shared_ptr<BlockCache> block_cache = nullptr);
            ~SSTableReader();
            SSTableReader(const SSTableReader&) = delete;
            SSTableReader& operator=(const SSTableReader&) = delete;
            bool isOpen() const;
            LookupResult get(std::string_view user_key, std::string& value) const;
            SSTableIterator newIterator() const;
            std::uint64_t dataBlockReadCount() const;
            std::size_t bloomMemoryUsage() const;
        private:
            struct IndexEntry
            {
                std::string last_internal_key;
                std::uint64_t block_offset;
                std::uint64_t block_size;
            };
            bool readAt(std::uint64_t offset, std::uint64_t size, std::string& output) const;
            bool loadIndex();
            bool readDataBlock(std::uint64_t offset, std::uint64_t size, std::string& output) const;
            int fd_ = -1;
            std::uint64_t file_size_ = 0;
            std::vector<IndexEntry> index_;
            mutable std::uint64_t data_block_read_count_ = 0;
            std::unique_ptr<BlockSource> data_block_source_;
            BloomFilter bloom_filter_;
            bool has_bloom_filter_ = false;
            friend class SSTableIterator;
    };

    class SSTableIterator
    {
        public:
            bool valid() const;
            bool ok() const;
            std::string_view internalKey() const;
            std::string_view value() const;
            void next();
        private:
            explicit SSTableIterator(const SSTableReader* reader);
            bool loadNextEntry();
            const SSTableReader* reader_ = nullptr;
            std::size_t block_index_ = 0;
            std::size_t block_position_ = 0;
            std::string block_; // data block in RAM
            std::string current_internal_key_;
            std::string current_value_;
            bool valid_ = false;
            bool ok_ = true;
            friend class SSTableReader;
    };
}
