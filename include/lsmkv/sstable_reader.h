#pragma once

#include <lsmkv/memtable.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsmkv
{
    class SSTableReader
    {
        public:
            explicit SSTableReader(std::string_view path);
            ~SSTableReader();
            SSTableReader(const SSTableReader&) = delete;
            SSTableReader& operator=(const SSTableReader&) = delete;
            bool isOpen() const;
            LookupResult get(std::string_view user_key, std::string& value) const;
        private:
            struct IndexEntry
            {
                std::string last_internal_key;
                std::uint64_t block_offset;
                std::uint64_t block_size;
            };
            bool readAt(std::uint64_t offset, std::uint64_t size, std::string& output) const;
            bool loadIndex();
            int fd_ = -1;
            std::uint64_t file_size_ = 0;
            std::vector<IndexEntry> index_;
    };
}