#include <catch_amalgamated.hpp>
#include <lsmkv/block_cache.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{
    class TestBlockSource : public lsmkv::BlockSource
    {
        public:
            explicit TestBlockSource(std::string data): data_(std::move(data)){}
            bool read(std::uint64_t offset, std::uint64_t size, std::string& output) override
            {
                read_count_++;
                if(offset > data_.size() || size > data_.size() - offset)
                    return false;
                output.assign(data_.data() + offset, static_cast<std::size_t>(size));
                return true;
            }
            std::uint64_t readCount() const
            {
                return read_count_;
            }
        private:
            std::string data_;
            std::uint64_t read_count_ = 0;
    };
}

TEST_CASE("BlockCache evicts the least recently used entry", "[block-cache]")
{
    lsmkv::BlockCache cache(8);
    const lsmkv::BlockCacheKey first{1, 0};
    const lsmkv::BlockCacheKey second{1, 4};
    const lsmkv::BlockCacheKey third{1, 8};
    cache.insert(first, "aaaa");
    cache.insert(second, "bbbb");
    REQUIRE(cache.capacity() == 8);
    REQUIRE(cache.currentSize() == 8);
    REQUIRE(cache.entryCount() == 2);
    REQUIRE(cache.hitRate() == 0.0);
    std::string block;
    REQUIRE(cache.get(first, block));
    REQUIRE(block == "aaaa");
    cache.insert(third, "cccc");
    REQUIRE(cache.currentSize() == 8);
    REQUIRE(cache.entryCount() == 2);
    REQUIRE_FALSE(cache.get(second, block));
    REQUIRE(cache.get(first, block));
    REQUIRE(block == "aaaa");
    REQUIRE(cache.get(third, block));
    REQUIRE(block == "cccc");
    REQUIRE(cache.hitCount() == 3);
    REQUIRE(cache.missCount() == 1);
    REQUIRE(cache.hitRate() == 0.75);
}

TEST_CASE("BlockCache updates entries and respects its capacity", "[block-cache]")
{
    lsmkv::BlockCache cache(5);
    const lsmkv::BlockCacheKey key{2, 0};
    cache.insert(key, "1234");
    REQUIRE(cache.currentSize() == 4);
    cache.insert(key, "xy");
    REQUIRE(cache.currentSize() == 2);
    REQUIRE(cache.entryCount() == 1);
    std::string block;
    REQUIRE(cache.get(key, block));
    REQUIRE(block == "xy");
    cache.insert(key, "123456");
    REQUIRE(cache.currentSize() == 0);
    REQUIRE(cache.entryCount() == 0);
    REQUIRE_FALSE(cache.get(key, block));
    lsmkv::BlockCache disabled_cache(0);
    disabled_cache.insert(key, "a");
    REQUIRE(disabled_cache.currentSize() == 0);
    REQUIRE(disabled_cache.entryCount() == 0);
}

TEST_CASE("BlockCache evicts entries after its capacity changes", "[block-cache]")
{
    lsmkv::BlockCache cache(8);
    cache.insert({1, 0}, "aaaa");
    cache.insert({1, 4}, "bbbb");
    cache.setCapacity(4);
    REQUIRE(cache.capacity() == 4);
    REQUIRE(cache.currentSize() == 4);
    REQUIRE(cache.entryCount() == 1);
    cache.setCapacity(0);
    REQUIRE(cache.currentSize() == 0);
    REQUIRE(cache.entryCount() == 0);
}

TEST_CASE("BlockCache separates identical offsets from different files", "[block-cache]")
{
    lsmkv::BlockCache cache(8);
    const lsmkv::BlockCacheKey first_file{3, 0};
    const lsmkv::BlockCacheKey second_file{7, 0};
    cache.insert(first_file, "aaaa");
    cache.insert(second_file, "bbbb");
    std::string block;
    REQUIRE(cache.get(first_file, block));
    REQUIRE(block == "aaaa");
    REQUIRE(cache.get(second_file, block));
    REQUIRE(block == "bbbb");
}

TEST_CASE("CachedBlockSource reads its source only after a cache miss", "[block-cache]")
{
    auto cache = std::make_shared<lsmkv::BlockCache>(8);
    auto source = std::make_unique<TestBlockSource>("abcdefgh");
    TestBlockSource* source_pointer = source.get();
    lsmkv::CachedBlockSource cached_source(5, std::move(source), cache);
    std::string block;
    REQUIRE(cached_source.read(0, 4, block));
    REQUIRE(block == "abcd");
    REQUIRE(source_pointer->readCount() == 1);
    REQUIRE(cache->missCount() == 1);
    REQUIRE(cache->hitCount() == 0);
    block.clear();
    REQUIRE(cached_source.read(0, 4, block));
    REQUIRE(block == "abcd");
    REQUIRE(source_pointer->readCount() == 1);
    REQUIRE(cache->missCount() == 1);
    REQUIRE(cache->hitCount() == 1);
    REQUIRE(cached_source.read(4, 4, block));
    REQUIRE(block == "efgh");
    REQUIRE(source_pointer->readCount() == 2);
    REQUIRE(cache->missCount() == 2);
    REQUIRE(cache->hitCount() == 1);
}
