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
#include <memory>
#include <utility>

namespace
{
    bool readFileRange(int fd, std::uint64_t file_size, std::uint64_t offset, std::uint64_t size, std::string& output)
    {
        if(fd == -1)
            return false;
        if(offset > file_size || size > file_size - offset)
            return false;
        if(size > std::numeric_limits<std::size_t>::max())
            return false;
        output.assign(static_cast<std::size_t>(size), '\0');
        std::size_t read = 0;
        while(read < output.size())
        {
            const ssize_t result = ::pread(fd, output.data() + read, output.size() - read, static_cast<off_t>(offset + read));
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

    class FileBlockSource : public lsmkv::BlockSource
    {
        public:
            FileBlockSource(int fd, std::uint64_t file_size, std::uint64_t* read_count)
            {
                fd_ = fd;
                file_size_ = file_size;
                read_count_ = read_count;
            }
            bool read(std::uint64_t offset, std::uint64_t size, std::string& output) override
            {
                if(read_count_ != nullptr)
                    (*read_count_)++;
                return readFileRange(fd_, file_size_, offset, size, output);
            }
        private:
            int fd_;
            std::uint64_t file_size_;
            std::uint64_t* read_count_;
    };
}

namespace lsmkv
{
    SSTableReader::SSTableReader(std::string_view path, std::uint64_t file_id, std::shared_ptr<BlockCache> block_cache)
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
        auto file_source = std::make_unique<FileBlockSource>(fd_, file_size_, &data_block_read_count_);
        if(block_cache)
            data_block_source_ = std::make_unique<CachedBlockSource>(file_id, std::move(file_source), std::move(block_cache));
        else
            data_block_source_ = std::move(file_source);
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
    std::uint64_t SSTableReader::dataBlockReadCount() const
    {
        return data_block_read_count_;
    }
    std::size_t SSTableReader::bloomMemoryUsage() const
    {
        if(!has_bloom_filter_)
            return 0;
        return bloom_filter_.memoryUsage();
    }
    SSTableIterator SSTableReader::newIterator() const
    {
        return SSTableIterator(this);
    }
    SSTableIterator::SSTableIterator(const SSTableReader* reader)
    {
        reader_ = reader;
        if(reader_ == nullptr || !reader_->isOpen())
        {
            ok_ = false;
            return;
        }
        loadNextEntry();
    }
    bool SSTableIterator::loadNextEntry()
    {
        valid_ = false;
        current_internal_key_.clear();
        current_value_.clear();
        while(true)
        {
            if(block_position_ < block_.size())
            {
                std::string_view block_input = block_;
                block_input.remove_prefix(block_position_);
                const std::size_t input_size = block_input.size();
                std::string_view internal_key;
                std::string_view value;
                if(!consumeLengthPrefixedSlice(block_input, internal_key) || !consumeLengthPrefixedSlice(block_input, value))
                {
                    ok_ = false;
                    return false;
                }
                ParsedInternalKey parsed_key;
                if(!parseInternalKey(internal_key, parsed_key))
                {
                    ok_ = false;
                    return false;
                }
                block_position_ += input_size - block_input.size();
                current_internal_key_.assign(internal_key);
                current_value_.assign(value);
                valid_ = true;
                return true;
            }
            if(block_index_ >= reader_->index_.size())
                return true;
            const SSTableReader::IndexEntry& index_entry = reader_->index_[block_index_];
            block_index_++;
            if(!reader_->readDataBlock(index_entry.block_offset, index_entry.block_size, block_))
            {
                ok_ = false;
                return false;
            }
            block_position_ = 0;
        }
    }
    void SSTableIterator::next()
    {
        if(!valid_)
            return;
        loadNextEntry();
    }
    bool SSTableIterator::valid() const
    {
        return valid_;
    }
    bool SSTableIterator::ok() const
    {
        return ok_;
    }
    std::string_view SSTableIterator::internalKey() const
    {
        if(!valid_)
            return {};
        return current_internal_key_;
    }
    std::string_view SSTableIterator::value() const
    {
        if(!valid_)
            return {};
        return current_value_;
    }
    bool SSTableReader::readAt(std::uint64_t offset, std::uint64_t size, std::string& output) const
    {
        return readFileRange(fd_, file_size_, offset, size, output);
    }
    bool SSTableReader::readDataBlock(std::uint64_t offset, std::uint64_t size, std::string& output) const
    {
        if(!data_block_source_)
            return false;
        return data_block_source_->read(offset, size, output);
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
        if(index_offset > footer_offset || index_size > footer_offset - index_offset)
            return false;
        const std::uint64_t index_end = index_offset + index_size;
        if(bloom_size == 0)
        {
            if(bloom_offset != 0 || index_end != footer_offset)
                return false;
        }
        else
        {
            if(bloom_offset > footer_offset || bloom_size > footer_offset - bloom_offset)
                return false;
            if(bloom_offset != index_end || bloom_size != footer_offset - bloom_offset)
                return false;
            std::string bloom_block;
            if(!readAt(bloom_offset, bloom_size, bloom_block))
                return false;
            if(!bloom_filter_.decode(bloom_block))
                return false;
            has_bloom_filter_ = true;
        }

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
        if(has_bloom_filter_ && !bloom_filter_.mayContain(user_key))
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
        if(!readDataBlock(index_entry->block_offset, index_entry->block_size, block))
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
