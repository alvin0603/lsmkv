#include <lsmkv/block_cache.h>

#include <functional>
#include <utility>

namespace lsmkv
{
    bool BlockCacheKey::operator==(const BlockCacheKey& other) const
    {
        return file_id == other.file_id && block_offset == other.block_offset;
    }
    std::size_t BlockCacheKeyHash::operator()(const BlockCacheKey& key) const
    {
        const std::size_t first = std::hash<std::uint64_t>{}(key.file_id);
        const std::size_t second = std::hash<std::uint64_t>{}(key.block_offset);
        return first ^ (second + 0x9E3779B9 + (first << 6) + (first >> 2));
    }
    BlockCache::BlockCache(std::size_t capacity)
    {
        capacity_ = capacity;
    }
    bool BlockCache::get(const BlockCacheKey& key, std::string& output)
    {
        const auto found = index_.find(key);
        if(found == index_.end())
        {
            miss_count_++;
            return false;
        }
        hit_count_++;
        entries_.splice(entries_.begin(), entries_, found->second);
        output = found->second->block;
        return true;
    }
    void BlockCache::insert(const BlockCacheKey& key, std::string_view block)
    {
        const auto found = index_.find(key);
        if(found != index_.end())
        {
            current_size_ -= found->second->block.size();
            if(block.size() > capacity_)
            {
                entries_.erase(found->second);
                index_.erase(found);
                return;
            }
            found->second->block.assign(block);
            current_size_ += found->second->block.size();
            entries_.splice(entries_.begin(), entries_, found->second);
            evict();
            return;
        }
        if(block.size() > capacity_)
            return;
        entries_.push_front({key, std::string(block)});
        index_[key] = entries_.begin();
        current_size_ += block.size();
        evict();
    }
    void BlockCache::evict()
    {
        while(current_size_ > capacity_)
        {
            const Entry& entry = entries_.back();
            current_size_ -= entry.block.size();
            index_.erase(entry.key);
            entries_.pop_back();
        }
    }
    std::size_t BlockCache::capacity() const
    {
        return capacity_;
    }
    std::size_t BlockCache::currentSize() const
    {
        return current_size_;
    }
    std::size_t BlockCache::entryCount() const
    {
        return entries_.size();
    }
    void BlockCache::setCapacity(std::size_t capacity)
    {
        capacity_ = capacity;
        evict();
    }
    std::uint64_t BlockCache::hitCount() const
    {
        return hit_count_;
    }
    std::uint64_t BlockCache::missCount() const
    {
        return miss_count_;
    }
    double BlockCache::hitRate() const
    {
        const std::uint64_t lookup_count = hit_count_ + miss_count_;
        if(lookup_count == 0)
            return 0.0;
        return static_cast<double>(hit_count_) / static_cast<double>(lookup_count);
    }
    CachedBlockSource::CachedBlockSource(std::uint64_t file_id, std::unique_ptr<BlockSource> source, std::shared_ptr<BlockCache> cache)
    {
        file_id_ = file_id;
        source_ = std::move(source);
        cache_ = std::move(cache);
    }
    bool CachedBlockSource::read(std::uint64_t offset, std::uint64_t size, std::string& output)
    {
        if(!source_)
            return false;
        const BlockCacheKey key{file_id_, offset};
        if(cache_ && cache_->get(key, output))
            return true;
        if(!source_->read(offset, size, output))
            return false;
        if(cache_)
            cache_->insert(key, output);
        return true;
    }
}