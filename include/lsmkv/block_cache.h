#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lsmkv
{
    inline constexpr std::size_t kDefaultBlockCacheCapacity = 8 * 1024 * 1024;

    class BlockSource
    {
        public:
            virtual ~BlockSource() = default;
            virtual bool read(std::uint64_t offset, std::uint64_t size, std::string& output) = 0;
    };
    struct BlockCacheKey
    {
        std::uint64_t file_id;
        std::uint64_t block_offset;
        bool operator==(const BlockCacheKey& other) const;
    };
    struct BlockCacheKeyHash
    {
        std::size_t operator()(const BlockCacheKey& key) const;
    };
    class BlockCache
    {
        public:
            explicit BlockCache(std::size_t capacity = kDefaultBlockCacheCapacity);
            bool get(const BlockCacheKey& key, std::string& output);
            void insert(const BlockCacheKey& key, std::string_view block);
            std::size_t capacity() const;
            std::size_t currentSize() const;
            std::size_t entryCount() const;
            std::uint64_t hitCount() const;
            std::uint64_t missCount() const;
            double hitRate() const;
        private:
            struct Entry
            {
                BlockCacheKey key;
                std::string block;
            };
            void evict();
            std::size_t capacity_;
            std::size_t current_size_ = 0;
            std::list<Entry> entries_;
            std::unordered_map<BlockCacheKey, std::list<Entry>::iterator, BlockCacheKeyHash> index_;
            std::uint64_t hit_count_ = 0;
            std::uint64_t miss_count_ = 0;
    };
    class CachedBlockSource : public BlockSource
    {
        public:
            CachedBlockSource(std::uint64_t file_id, std::unique_ptr<BlockSource> source, std::shared_ptr<BlockCache> cache);
            bool read(std::uint64_t offset, std::uint64_t size, std::string& output) override;
        private:
            std::uint64_t file_id_;
            std::unique_ptr<BlockSource> source_;
            std::shared_ptr<BlockCache> cache_;
    };
}