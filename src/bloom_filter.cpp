#include <lsmkv/bloom_filter.h>
#include <lsmkv/coding.h>

#include <algorithm>
#include <limits>

namespace lsmkv
{
    BloomFilter::BloomFilter(std::size_t bits_per_key, std::uint32_t num_hashes)
    {
        bits_per_key_ = bits_per_key;
        num_hashes_ = num_hashes;
    }
    std::uint64_t BloomFilter::hash(std::string_view user_key, std::uint64_t seed)
    {
        /* FNV-1a algorithm */
        std::uint64_t result = 14695981039346656037ULL ^ seed;
        for(char byte: user_key)
        {
            result ^= static_cast<std::uint8_t>(byte);
            result *= 1099511628211ULL;
        }
        return result;
    }
    bool BloomFilter::add(std::string_view user_key)
    {
        if(finished_)
            return false;
        const std::uint64_t first = hash(user_key, 0);
        std::uint64_t second = hash(user_key, 0x9E3779B97F4A7C15ULL);
        if(second == 0)
            second = 0x9E3779B97F4A7C15ULL;
        hashes_.push_back({first, second});
        return true;
    }
    void BloomFilter::setBit(std::uint64_t index)
    {
        const std::size_t byte_index = static_cast<std::size_t>(index / 8);
        const std::uint8_t mask = static_cast<std::uint8_t>(1U << (index % 8));
        const std::uint8_t value = static_cast<std::uint8_t>(bits_[byte_index]);
        bits_[byte_index] = static_cast<char>(value | mask);
    }
    bool BloomFilter::getBit(std::uint64_t index) const
    {
        const std::size_t byte_index = static_cast<std::size_t>(index / 8);
        const std::uint8_t mask = static_cast<std::uint8_t>(1U << (index % 8));
        const std::uint8_t value = static_cast<std::uint8_t>(bits_[byte_index]);
        return (value & mask) != 0;
    }
    bool BloomFilter::finish()
    {
        if(finished_ || bits_per_key_ == 0 || num_hashes_ == 0 || num_hashes_ > kMaxBloomHashCount)
            return false;
        if(hashes_.empty())
        {
            finished_ = true;
            return true;
        }
        const std::uint64_t key_count = static_cast<std::uint64_t>(hashes_.size());
        const std::uint64_t bits_per_key = static_cast<std::uint64_t>(bits_per_key_);
        if(key_count > std::numeric_limits<std::uint64_t>::max() / bits_per_key)
            return false;
        std::uint64_t target_bits = key_count * bits_per_key;
        target_bits = std::max<std::uint64_t>(target_bits, 64);
        if(target_bits > std::numeric_limits<std::uint64_t>::max() - 7)
            return false;
        const std::uint64_t byte_count = (target_bits + 7) / 8;
        if(byte_count > std::numeric_limits<std::size_t>::max())
            return false;
        num_bits_ = byte_count * 8;
        bits_.assign(static_cast<std::size_t>(byte_count), '\0');
        for(const HashPair& hashes: hashes_)
        {
            for(std::uint32_t i = 0; i < num_hashes_; i++)
            {
                const std::uint64_t combined_hash = hashes.first + static_cast<std::uint64_t>(i) * hashes.second;
                setBit(combined_hash % num_bits_);
            }
        }
        hashes_.clear();
        finished_ = true;
        return true;
    }
    bool BloomFilter::mayContain(std::string_view user_key) const
    {
        if(!finished_)
            return true;
        if(num_bits_ == 0)
            return false;
        const std::uint64_t first = hash(user_key, 0);
        std::uint64_t second = hash(user_key, 0x9E3779B97F4A7C15ULL);
        if(second == 0)
            second = 0x9E3779B97F4A7C15ULL;
        for(std::uint32_t i = 0; i < num_hashes_; i++)
        {
            const std::uint64_t combined_hash = first + static_cast<std::uint64_t>(i) * second;
            if(!getBit(combined_hash % num_bits_))
                return false;
        }
        return true;
    }
    bool BloomFilter::encode(std::string& output) const
    {
        if(!finished_)
            return false;
        std::string encode;
        appendFixed32(encode, num_hashes_);
        appendFixed64(encode, num_bits_);
        encode.append(bits_);
        output.append(encode);
        return true;
    }
    bool BloomFilter::decode(std::string_view input)
    {
        std::string_view remaining_input = input;
        std::uint32_t num_hashes = 0;
        std::uint64_t num_bits = 0;
        if(!consumeFixed32(remaining_input, num_hashes) || !consumeFixed64(remaining_input, num_bits))
            return false;
        if(num_hashes == 0 || num_hashes > kMaxBloomHashCount)
            return false;
        const std::uint64_t byte_count = num_bits / 8 + (num_bits % 8 != 0);
        if(byte_count > std::numeric_limits<std::size_t>::max())
            return false;
        if(remaining_input.size() != static_cast<std::size_t>(byte_count))
            return false;
        num_hashes_ = num_hashes;
        num_bits_ = num_bits;
        bits_.assign(remaining_input);
        hashes_.clear();
        finished_ = true;
        return true;
    }
}
