#include <lsmkv/sstable_reader.h>
#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>
#include <lsmkv/sstable_format.h>

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

namespace lsmkv
{
    SSTableReader::SSTableReader(std::string_view path)
    {
        const std::string path_string(path);
        fd_ = ::open(path_string.c_str(), O_RDONLY);
        if(fd_ == -1)
            return;
        struct stat file_stat;
        if(::fstat(fd_, &file_stat) == -1 || file_stat.st_size < 0)
        {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        file_size_ = static_cast<std::uint64_t>(file_stat.st_size);
        if(!loadIndex())
        {
            ::close(fd_);
            fd_ = -1;
        }
    }
    SSTableReader::~SSTableReader()
    {
        if(fd_ != -1)
            ::close(fd_);
    }
    bool SSTableReader::isOpen() const
    {
        return fd_ != -1;
    }
    bool SSTableReader::readAt(std::uint64_t offset, std::uint64_t size, std::string& output) const
    {
        if(fd_ == -1)
            return false;
        if(offset > file_size_ || size > file_size_ - offset)
            return false;
        if(size > std::numeric_limits<std::size_t>::max())
            return false;
        output.assign(static_cast<std::size_t>(size), '\0');
        std::size_t read = 0;
        while(read < output.size())
        {
            const ssize_t result = ::pread(fd_, output.data() + read, output.size() - read, static_cast<off_t>(offset + read));
            if(result == -1)
            {
                if(errno == EINTR)
                    continue;
                return false;
            }
            if(result == 0)
                return false;
            read += static_cast<std::size_t>(result);
        }
        return true;
    }
    bool SSTableReader::loadIndex()
    {
        if(file_size_ < kSSTableFooterSize)
            return false;
        const std::uint64_t footer_offset = file_size_ - kSSTableFooterSize;
        std:: string footer;
        if(!readAt(footer_offset, kSSTableFooterSize, footer))
            return false;
        std::string_view footer_input= footer;
        std::uint64_t index_offset = 0;
        std::uint64_t index_size = 0;
        std::uint64_t bloom_offset = 0;
        std::uint64_t bloom_size = 0;
        std::uint64_t magic = 0;
        if(!consumeFixed64(footer_input, index_offset) ||
           !consumeFixed64(footer_input, index_size) ||
           !consumeFixed64(footer_input, bloom_offset) ||
           !consumeFixed64(footer_input, bloom_size) ||
           !consumeFixed64(footer_input, magic))
            return false;
        if(!footer_input.empty())
            return false;
        if(magic != kSSTableMagic)
            return false;
        if(bloom_offset != 0 || bloom_size != 0)
            return false;
        if(index_offset > footer_offset || index_size > footer_offset - index_offset)
            return false;
        
        // read index block and parse entries
        std::string index_block;
        if(!readAt(index_offset, index_size, index_block))
            return false;
        std::string_view index_input = index_block;
        InternalKeyComparator comparator;
        while(!index_input.empty())
        {
            std::string_view last_internal_key;
            std::uint64_t block_offset = 0;
            std::uint64_t block_size = 0;
            if(!consumeLengthPrefixedSlice(index_input, last_internal_key) ||
               !consumeFixed64(index_input, block_offset) ||
               !consumeFixed64(index_input, block_size))
                return false;
            ParsedInternalKey parsed_key;
            if(!parseInternalKey(last_internal_key, parsed_key))
                return false;
            if(block_size == 0)
                return false;
            if(block_offset > index_offset || block_size > index_offset - block_offset)
                return false;
            if(!index_.empty() && !comparator(index_.back().last_internal_key, last_internal_key))
                return false;
            index_.push_back({std::string(last_internal_key), block_offset, block_size});
        }
        return true;
    }
    LookupResult SSTableReader::get(std::string_view user_key, std::string& value) const
    {
        if(fd_ == -1 || index_.empty())
            return LookupResult::kNotFound;
        std::string target;
        if(!appendInternalKey(target, user_key, kMaxSequenceNumber, ValueType::kPut))
            return LookupResult::kNotFound;
        InternalKeyComparator comparator;

        // binary search for the index entry
        const std::string_view target_view = target;
        const auto index_entry = std::lower_bound(index_.begin(), index_.end(), target_view, [&comparator](const IndexEntry& entry, std::string_view target_key)
        {
            return comparator(entry.last_internal_key, target_key);
        });
        if(index_entry == index_.end())
            return LookupResult::kNotFound;

        // read the data block
        std::string block;
        if(!readAt(index_entry->block_offset, index_entry->block_size, block))
            return LookupResult::kNotFound;
        std::string_view block_input = block;

        // linear search within the block for the target
        while(!block_input.empty())
        {
            std::string_view internal_key;
            std::string_view entry_value;
            if(!consumeLengthPrefixedSlice(block_input, internal_key) ||!consumeLengthPrefixedSlice(block_input, entry_value))
                return LookupResult::kNotFound;
            ParsedInternalKey parsed_key;
            if(!parseInternalKey(internal_key, parsed_key))
                return LookupResult::kNotFound;
            if(parsed_key.user_key < user_key)
                continue;
            if(parsed_key.user_key > user_key)
                return LookupResult::kNotFound;
            if(parsed_key.type == ValueType::kDelete)
                return LookupResult::kDeleted;
            value.assign(entry_value);
            return LookupResult::kFound;
        }
        return LookupResult::kNotFound;
    }
}