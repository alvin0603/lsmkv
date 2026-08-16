#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsmkv
{
    // total bits: number of keys * 10 bits
    // bits set by keys: 7
    inline constexpr std::size_t kDefaultBloomBitsPerKey = 10;
    inline constexpr std::uint32_t kDefaultBloomHashCount = 7;
    inline constexpr std::uint32_t kMaxBloomHashCount = 30; // crash prevention

    class BloomFilter
    {
        public:
            explicit BloomFilter(std::size_t bits_per_key = kDefaultBloomBitsPerKey, std::uint32_t num_hashes = kDefaultBloomHashCount);
            bool add(std::string_view user_key);
            bool finish();
            bool mayContain(std::string_view user_key) const;
            bool encode(std::string& output) const;
            bool decode(std::string_view input);
        private:
            struct HashPair
            {
                std::uint64_t first;
                std::uint64_t second;
            };
            static std::uint64_t hash(std::string_view user_key, std::uint64_t seed);
            void setBit(std::uint64_t index);
            bool getBit(std::uint64_t index) const;
            std::size_t bits_per_key_;
            std::uint32_t num_hashes_;
            std::uint64_t num_bits_ = 0;
            std::vector<HashPair> hashes_;
            std::string bits_;
            bool finished_ = false;
    };
}