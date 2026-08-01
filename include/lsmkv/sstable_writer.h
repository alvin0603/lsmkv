#pragma once

#include <lsmkv/internal_key.h>
#include <lsmkv/sstable_format.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lsmkv
{
    class SSTableWriter
    {
        public:
            explicit SSTableWriter(std::string_view path, std::size_t target_block_size = kDefaultDataBlockSize);
            ~SSTableWriter();
            SSTableWriter(const SSTableWriter&) = delete;
            SSTableWriter& operator=(const SSTableWriter&) = delete;
            bool isOpen() const;
            bool add(std::string_view internal_key, std::string_view value);
            bool finish();
        private:
            bool writeAll(std::string_view data);
            bool flushBlock();
            int fd_ = -1;
            std::size_t target_block_size_;
            std::uint64_t file_offset_ = 0;
            std::string current_block_;
            std::string current_block_last_key_;
            std::string index_block_;
            std::string last_added_key_;
            bool has_last_key_ = false;
            bool finished_ = false;
    };
}