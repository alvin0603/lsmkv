#include <lsmkv/manifest.h>
#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>

#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace 
{
    bool validateManifestState(const lsmkv::ManifestState& state)
    {
        if(state.next_file_id == 0)
            return false;
        if(state.last_sequence > lsmkv::kMaxSequenceNumber)
            return false;
        if(state.tables.size() > std::numeric_limits<std::uint32_t>::max())
            return false;
         std::unordered_set<std::uint64_t> file_id;
         lsmkv::InternalKeyComparator comparator;
         for(const auto& table : state.tables)
         {
            if(table.file_id == 0 || table.file_id >= state.next_file_id || table.file_size == 0)
                return false;
            if(!file_id.insert(table.file_id).second)
                return false;
            lsmkv::ParsedInternalKey tmp_smallest;
            lsmkv::ParsedInternalKey tmp_largest;
            if(!lsmkv::parseInternalKey(table.smallest_key, tmp_smallest) || !lsmkv::parseInternalKey(table.largest_key, tmp_largest))
                return false;
            if(comparator(table.largest_key, table.smallest_key))
                return false;
         }
         return true;
    }
}

namespace lsmkv
{
    bool encodeManifest(const ManifestState& state, std::string& output)
    {
        if(!validateManifestState(state))
            return false;
        std::string encode;
        appendFixed64(encode, kManifestMagic);
        appendFixed64(encode, state.next_file_id);
        appendFixed64(encode, state.last_sequence);
        appendFixed64(encode, state.durable_wal_epoch);
        appendFixed32(encode, static_cast<std::uint32_t>(state.tables.size()));
        for(const TableMetaData& table : state.tables)
        {
            appendFixed64(encode, table.file_id);
            appendFixed32(encode, table.level);
            appendFixed64(encode, table.file_size);
            if(!appendLengthPrefixedSlice(encode, table.smallest_key) || !appendLengthPrefixedSlice(encode, table.largest_key))
                return false;
        }
        output.append(encode);
        return true;
    }
    bool decodeManifest(std::string_view input, ManifestState& state)
    {
        std::string_view remaining_input = input;
        std::uint64_t magic = 0;
        ManifestState decode;
        std::uint32_t num_tables = 0;
        if(!consumeFixed64(remaining_input, magic) || !consumeFixed64(remaining_input, decode.next_file_id) ||!consumeFixed64(remaining_input, decode.last_sequence) ||
           !consumeFixed64(remaining_input, decode.durable_wal_epoch) || !consumeFixed32(remaining_input, num_tables))
            return false;
        if(magic != kManifestMagic)
            return false;
        for(std::uint32_t i = 0; i < num_tables; i++)
        {
            TableMetaData table;
            std::string_view smallest_internal_key;
            std::string_view largest_internal_key;
            if(!consumeFixed64(remaining_input, table.file_id) || !consumeFixed32(remaining_input, table.level) ||
               !consumeFixed64(remaining_input, table.file_size) || !consumeLengthPrefixedSlice(remaining_input, smallest_internal_key) ||
               !consumeLengthPrefixedSlice(remaining_input, largest_internal_key))
                return false;
            table.smallest_key.assign(smallest_internal_key);
            table.largest_key.assign(largest_internal_key);
            decode.tables.push_back(std::move(table));
        }
        if(!remaining_input.empty())
            return false;
        if(!validateManifestState(decode))
            return false;
        state = std::move(decode);
        return true;
    }
    bool writeAll(int fd, std::string_view data)
    {
        std::size_t total_written = 0;
        while(total_written < data.size())
        {
            ssize_t written = ::write(fd, data.data() + total_written, data.size() - total_written);
            if(written == -1)
            {
                if(errno == EINTR)
                    continue;
                return false;
            }
            if(written == 0)
                return false;
            total_written += static_cast<std::size_t>(written);
        }
        return true;
    }
    bool readAll(int fd, std::size_t file_size, std::string& output)
    {
        output.resize(file_size);
        std::size_t total_read = 0;
        while(total_read < file_size)
        {
            ssize_t bytes_read = ::read(fd, output.data() + total_read, file_size - total_read);
            if(bytes_read == -1)
            {
                if(errno == EINTR)
                    continue;
                return false;
            }
            if(bytes_read == 0)
                return false;
            total_read += static_cast<std::size_t>(bytes_read);
        }
        return true;
    }
    bool syncFile(int fd)
    {
        while(::fsync(fd) == -1)
        {
            if(errno == EINTR)
                continue;
            return false;
        }
        return true;
    }
    bool loadManifest(std::string_view directory, ManifestState& state)
    {
        std::filesystem::path manifest_path = std::filesystem::path(directory) / "MANIFEST";
        int fd = ::open(manifest_path.c_str(), O_RDONLY);
        if(fd == -1)
        {
            if(errno == ENOENT)
            {
                state = ManifestState{}; // new DB
                return true;
            }
            return false;
        }
        struct stat file_stat;
        if(::fstat(fd, &file_stat) == -1 || file_stat.st_size < 0)
        {
            ::close(fd);
            return false;
        }
        if(static_cast<std::uintmax_t>(file_stat.st_size) > std::numeric_limits<std::size_t>::max())
        {
            ::close(fd);
            return false;
        }
        std::string encoded_manifest;
        bool success = readAll(fd, static_cast<std::size_t>(file_stat.st_size), encoded_manifest);
        if(::close(fd) == -1)
            success = false;
        if(!success)
            return false;
        return decodeManifest(encoded_manifest, state);
    }
    bool saveManifest(std::string_view directory, const ManifestState& state)
    {
        std::string encoded_manifest;
        if(!encodeManifest(state, encoded_manifest))
            return false;
        std::filesystem::path directory_path(directory);
        std::filesystem::path tmp_path = directory_path / "MANIFEST.tmp";
        std::filesystem::path manifest_path = directory_path / "MANIFEST";
        int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd == -1)
            return false;
        bool success = writeAll(fd, encoded_manifest);
        if(success)
            success = syncFile(fd);
        if(::close(fd) == -1)
            success = false;
        if(!success)
        {
            ::unlink(tmp_path.c_str());
            return false;
        }
        if(::rename(tmp_path.c_str(), manifest_path.c_str()) == -1)
        {
            ::unlink(tmp_path.c_str());
            return false;
        }
        int directory_fd = ::open(directory_path.c_str(), O_RDONLY | O_DIRECTORY);
        if(directory_fd == -1)
            return false;
        success = syncFile(directory_fd);
        if(::close(directory_fd) == -1)
            success = false;
        return success;
    }
}
