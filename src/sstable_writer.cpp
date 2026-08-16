#include <lsmkv/sstable_writer.h>
#include <lsmkv/coding.h>
#include "internal/file_io.h"

#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace lsmkv
{
    SSTableWriter::SSTableWriter(std::string_view path, std::size_t target_block_size, std::size_t bloom_bits_per_key, std::uint32_t bloom_num_hashes): bloom_filter_(bloom_bits_per_key, bloom_num_hashes)
    {
        target_block_size_ = target_block_size;
        if(target_block_size_ == 0 || bloom_bits_per_key == 0 || bloom_num_hashes == 0 || bloom_num_hashes > kMaxBloomHashCount)
            return;
        const std::string path_string(path);
        fd_ = ::open(path_string.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    }
    SSTableWriter::~SSTableWriter()
    {
        if(fd_ != -1)
            ::close(fd_);
    }
    bool SSTableWriter::isOpen() const
    {
        return fd_ != -1;
    }
    bool SSTableWriter::add(std::string_view internal_key, std::string_view value)
    {
        if(fd_ == -1 || finished_)
            return false;
        ParsedInternalKey parsed_key;
        if(!parseInternalKey(internal_key, parsed_key))
            return false;
        InternalKeyComparator comparator;
        if(has_last_key_ && !comparator(last_added_key_, internal_key))
            return false;
        std::string entry;
        if(!appendLengthPrefixedSlice(entry, internal_key))
            return false;
        if(!appendLengthPrefixedSlice(entry, value))
            return false;
        if(!current_block_.empty() && current_block_.size() + entry.size() > target_block_size_)
        {
            if(!flushBlock())
                return false;
        }
        if(!has_last_bloom_user_key_ || last_bloom_user_key_ != parsed_key.user_key)
        {
            if(!bloom_filter_.add(parsed_key.user_key))
                return false;
            last_bloom_user_key_.assign(parsed_key.user_key);
            has_last_bloom_user_key_ = true;
        }
        current_block_.append(entry);
        current_block_last_key_.assign(internal_key);
        last_added_key_.assign(internal_key);
        has_last_key_ = true;
        return true;
    }
    bool SSTableWriter::flushBlock()
    {
        if(current_block_.empty())
            return true;
        const std::uint64_t block_offset = file_offset_;
        const std::uint64_t block_size = current_block_.size();
        if(!writeAll(fd_, current_block_))
            return false;
        if(!appendLengthPrefixedSlice(index_block_, current_block_last_key_))
            return false;
        appendFixed64(index_block_, block_offset);
        appendFixed64(index_block_, block_size);
        file_offset_ += block_size;
        current_block_.clear();
        current_block_last_key_.clear();
        return true;
    }
    bool SSTableWriter::finish()
    {
        if(fd_ == -1 || finished_)
            return false;
        if(!flushBlock())
            return false;
        const std::uint64_t index_offset = file_offset_;
        const std::uint64_t index_size = index_block_.size();
        if(!writeAll(fd_, index_block_))
            return false;
        file_offset_ += index_size;
        if(!bloom_filter_.finish())
            return false;
        std::string bloom_block;
        if(!bloom_filter_.encode(bloom_block))
            return false;
        const std::uint64_t bloom_offset = file_offset_;
        const std::uint64_t bloom_size = bloom_block.size();
        if(!writeAll(fd_, bloom_block))
            return false;
        file_offset_ += bloom_size;
        std::string footer;
        appendFixed64(footer, index_offset);
        appendFixed64(footer, index_size);
        appendFixed64(footer, bloom_offset);
        appendFixed64(footer, bloom_size);
        appendFixed64(footer, kSSTableMagic);
        if(!writeAll(fd_, footer))
            return false;
        if(!syncFile(fd_))
            return false;
        finished_ = true;
        return true;
    }
}
