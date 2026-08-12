#include <lsmkv/db.h>
#include <lsmkv/wal_reader.h>
#include <lsmkv/sstable_writer.h>
#include "internal/file_names.h"

#include <fcntl.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <sys/file.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace
{
    struct WalFile
    {
        std::uint64_t epoch;
        std::filesystem::path path;
    };
}

namespace lsmkv
{
    DB::~DB()
    {
        close();
    }
    void DB::close()
    {
        wal_writer_.reset();
        opened_tables_.clear();
        if(lock_fd_ != -1)
        {
            ::close(lock_fd_);
            lock_fd_ = -1;
        }
        open_ = false;
    }
    bool DB::isOpen() const
    {
        return open_;
    }
    std::unique_ptr<DBIterator> DB::newIterator() const
    {
        if(!open_)
            return nullptr;
        return std::unique_ptr<DBIterator>(new DBIterator(this));
    }
    std::unique_ptr<DB> DB::open(std::string_view path, SyncMode sync_mode, std::size_t sync_interval, std::size_t memtable_flush_size)
    {
        auto db = std::unique_ptr<DB>(new DB());
        if(memtable_flush_size == 0)
            return nullptr;
        db->directory_ = std::string(path);
        db->sync_mode_ = sync_mode;
        db->sync_interval_ = sync_interval;
        db->memtable_flush_size_ = memtable_flush_size;
        const std::filesystem::path directory{std::string(path)};
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if(error)
            return nullptr;
        
        // create and lock the lock file
        const std::filesystem::path lock_path = lockFilePath(directory);
        const std::string lock_path_string = lock_path.string();
        db->lock_fd_ = ::open(lock_path_string.c_str(), O_RDWR | O_CREAT, 0644);
        if(db->lock_fd_ == -1)
            return nullptr;
        if(::flock(db->lock_fd_, LOCK_EX | LOCK_NB) == -1)
            return nullptr;
        
        // load the manifest, validate the SSTable refered by the manifest, and scan for all WAL files
        if(!loadManifest(directory.string(), db->manifest_state_))
            return nullptr;
        std::unordered_set<std::uint64_t> live_table_ids;
        for(const TableMetaData& table : db->manifest_state_.tables)
        {
            live_table_ids.insert(table.file_id);
            const std::filesystem::path table_path = sstableFilePath(directory, table.file_id);
            std::error_code table_error;
            if(!std::filesystem::is_regular_file(table_path, table_error) || table_error)
                return nullptr;
            const std::uintmax_t actual_file_size = std::filesystem::file_size(table_path, table_error);
            if(table_error || actual_file_size != table.file_size)
                return nullptr;
            auto reader = std::make_unique<SSTableReader>(table_path.string());
            if(!reader->isOpen())
                return nullptr;
            db->opened_tables_.push_back({table, std::move(reader)});
        }
        db->sortOpenedTables();
        std::vector<WalFile> wal_files;
        std::filesystem::directory_iterator it(directory, error);
        std::filesystem::directory_iterator end;
        while(!error && it != end)
        {
            const std::filesystem::path file_path = it->path();
            it.increment(error);
            std::uint64_t table_id = 0;
            if(parseSSTableFileId(file_path, table_id))
            {
                /* Discard the files that are not referenced by the manifest */
                if(!live_table_ids.contains(table_id))
                {
                    std::error_code remove_error;
                    std::filesystem::remove(file_path, remove_error);
                    if(remove_error)
                        return nullptr;
                }
                continue;
            }
            std::uint64_t epoch = 0;
            if(!parseWalEpoch(file_path, epoch))
                continue;
            if(epoch <= db->manifest_state_.durable_wal_epoch)
            {
                /* In case the old WAL has not been deleted*/
                std::error_code remove_error;
                std::filesystem::remove(file_path, remove_error);
                if(remove_error)
                    return nullptr;
                continue;
            }
            wal_files.push_back({epoch, file_path});
        }
        if(error)
            return nullptr;

        // sort by epoch
        std::sort(wal_files.begin(), wal_files.end(),[](const WalFile& left, const WalFile& right){return left.epoch < right.epoch;});

        // replay all valid WAL files
        std::uint64_t max_replayed_sequence = 0;
        for(const WalFile& wal_file : wal_files)
        {
            WalReader wal_reader(wal_file.path.string());
            if(!wal_reader.isOpen())
                return nullptr;
            WalReplayResult replay_result;
            if(!wal_reader.replay(db->memtable_, replay_result))
                return nullptr;
            max_replayed_sequence = std::max(max_replayed_sequence, replay_result.max_sequence);
        }

        // set up active WAL epoch
        if(wal_files.empty())
        {
            if(db->manifest_state_.durable_wal_epoch == std::numeric_limits<std::uint64_t>::max())
                return nullptr;
            db->active_wal_epoch_ = db->manifest_state_.durable_wal_epoch + 1;
        }
        else
            db->active_wal_epoch_ = wal_files.back().epoch;

        // open the active WAL writer
        const std::filesystem::path wal_path = walFilePath(directory, db->active_wal_epoch_);
        db->wal_writer_ = std::make_unique<WalWriter>(wal_path.string(), db->sync_mode_, db->sync_interval_);
        if(!db->wal_writer_->isOpen())
            return nullptr;

        // seu up the next sequence number
        const std::uint64_t last_sequence = std::max(db->manifest_state_.last_sequence,max_replayed_sequence);
        db->next_sequence_ = last_sequence + 1;
        db->open_ = true;

        // In case the DB is opened with excessive RAM
        if(db->memtable_.approximateMemoryUsage() >= db->memtable_flush_size_)
        {
            if(!db->flushMemTable())
                return nullptr;
        }
        return db;
    }
    bool DB::put(std::string_view user_key, std::string_view value)
    {
        if(!open_ || !wal_writer_)
            return false;
        if(next_sequence_ > kMaxSequenceNumber)
            return false;
        const std::uint64_t sequence = next_sequence_;
        if(!wal_writer_->append(sequence, ValueType::kPut, user_key, value))
            return false;
        if(!memtable_.add(sequence, ValueType::kPut, user_key, value))
            return false;
        next_sequence_++;
        if(memtable_.approximateMemoryUsage() >= memtable_flush_size_)
            return flushMemTable();
        return true;
    }
    LookupResult DB::get(std::string_view user_key, std::string& value) const
    {
        if(!open_)
            return LookupResult::kNotFound;
        LookupResult result = memtable_.get(user_key, value);
        if(result != LookupResult::kNotFound)
            return result;
        if(immutable_memtable_)
        {
            result = immutable_memtable_->get(user_key, value);
            if(result != LookupResult::kNotFound)
                return result;
        }
        for(const OpenedTable& table: opened_tables_)
        {
            result = table.reader->get(user_key, value);
            if(result != LookupResult::kNotFound)
                return result;
        }
        return LookupResult::kNotFound;
    }
    bool DB::deleteKey(std::string_view user_key)
    {
        if(!open_ || !wal_writer_)
            return false;
        if(next_sequence_ > kMaxSequenceNumber)
            return false;
        const std::uint64_t sequence = next_sequence_;
        if(!wal_writer_->append(sequence, ValueType::kDelete, user_key, ""))
            return false;
        if(!memtable_.add(sequence, ValueType::kDelete, user_key, ""))
            return false;
        next_sequence_++;
        if(memtable_.approximateMemoryUsage() >= memtable_flush_size_)
            return flushMemTable();
        return true;
    }
    bool DB::flush()
    {
        return flushMemTable();
    }
    bool DB::flushMemTable()
    {
        if(!open_ || !wal_writer_)
            return false;
        if(immutable_memtable_)
            return handleFlushFailure();
        if(memtable_.empty())
            return true;
        if(active_wal_epoch_ == std::numeric_limits<std::uint64_t>::max())
            return handleFlushFailure();
        if(manifest_state_.next_file_id == std::numeric_limits<std::uint64_t>::max())
            return handleFlushFailure();

        // create a new WAL file
        const std::uint64_t immutable_wal_epoch = active_wal_epoch_;
        const std::uint64_t new_wal_epoch = active_wal_epoch_ + 1;
        const std::filesystem::path new_wal_path = walFilePath(std::filesystem::path(directory_), new_wal_epoch);
        auto new_wal_writer = std::make_unique<WalWriter>(new_wal_path.string(), sync_mode_, sync_interval_);
        if(!new_wal_writer->isOpen())
            return handleFlushFailure();

        // freeze the current MemTable and switch to the new WAL writer
        immutable_memtable_ = std::make_unique<MemTable>(std::move(memtable_));
        memtable_ = MemTable{};
        wal_writer_ = std::move(new_wal_writer);
        active_wal_epoch_ = new_wal_epoch;

        // write the immutable memtable to the new SSTable
        const std::uint64_t table_id = manifest_state_.next_file_id;
        const std::filesystem::path table_path = sstableFilePath(std::filesystem::path(directory_), table_id);
        bool table_written = false;
        {
            SSTableWriter writer(table_path.string());
            if(writer.isOpen())
            {
                table_written = true;
                for(const auto& [internal_key, value] : *immutable_memtable_)
                {
                    if(!writer.add(internal_key, value))
                    {
                        table_written = false;
                        break;
                    }
                }
                if(table_written)
                    table_written = writer.finish();
            }
        }
        if(!table_written)
        {
            /* Manifest has not been updated*/
            std::error_code remove_error;
            std::filesystem::remove(table_path, remove_error);
            return handleFlushFailure();
        }

        // Create the new Table Metadata for a new Manifest and validate the SSTable
        std::error_code file_size_error;
        const std::uintmax_t table_file_size = std::filesystem::file_size(table_path, file_size_error);
        if(file_size_error || table_file_size > std::numeric_limits<std::uint64_t>::max())
            return handleFlushFailure();
        TableMetaData table_metadata;
        table_metadata.file_id = table_id;
        table_metadata.level = 0;
        table_metadata.file_size = static_cast<std::uint64_t>(table_file_size);
        table_metadata.smallest_key = immutable_memtable_->begin()->first;
        table_metadata.largest_key = std::prev(immutable_memtable_->end())->first;
        auto table_reader = std::make_unique<SSTableReader>(table_path.string());
        if(!table_reader->isOpen())
            return handleFlushFailure(); // double check the SSTable
        ManifestState tmp_manifest_state = manifest_state_;
        tmp_manifest_state.next_file_id = table_id + 1;
        tmp_manifest_state.last_sequence = next_sequence_ - 1;
        tmp_manifest_state.durable_wal_epoch = immutable_wal_epoch;
        tmp_manifest_state.tables.push_back(std::move(table_metadata));

        // atomic commit of the new Manifest and clean up the old WAL and MeMTable
        if(!saveManifest(directory_, tmp_manifest_state))
            return handleFlushFailure();
        manifest_state_ = std::move(tmp_manifest_state);
        opened_tables_.push_back({manifest_state_.tables.back(), std::move(table_reader)});
        sortOpenedTables();
        const std::filesystem::path immutable_wal_path = walFilePath(std::filesystem::path(directory_), immutable_wal_epoch);
        std::error_code remove_error;
        std::filesystem::remove(immutable_wal_path, remove_error);
        immutable_memtable_.reset();
        return true;
    }
    bool DB::handleFlushFailure()
    {
        close();
        return false;
    }
    void DB::sortOpenedTables()
    {
        std::sort(opened_tables_.begin(), opened_tables_.end(), [](const OpenedTable& left, const OpenedTable& right)
        {
            if(left.metadata.level != right.metadata.level)
                return left.metadata.level < right.metadata.level;
            return left.metadata.file_id > right.metadata.file_id;
        });
    }
}
