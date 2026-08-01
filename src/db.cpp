#include <lsmkv/db.h>
#include <lsmkv/wal_reader.h>

#include <fcntl.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <sys/file.h>
#include <unistd.h>

namespace lsmkv
{
    DB::~DB()
    {
        close();
    }
    void DB::close()
    {
        wal_writer_.reset();
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
    std::unique_ptr<DB> DB::open(std::string_view path, SyncMode sync_mode, std::size_t sync_interval)
    {
        auto db = std::unique_ptr<DB>(new DB());
        const std::filesystem::path directory{std::string(path)};
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if(error)
            return nullptr;
        
        // create and lock the lock file
        const std::filesystem::path lock_path = directory / "LOCK";
        const std::string lock_path_string = lock_path.string();
        db->lock_fd_ = ::open(lock_path_string.c_str(), O_RDWR | O_CREAT, 0644);
        if(db->lock_fd_ == -1)
            return nullptr;
        if(::flock(db->lock_fd_, LOCK_EX | LOCK_NB) == -1)
            return nullptr;
        
        // create the WAL writer
        const std::filesystem::path wal_path = directory / "1.wal";
        const std::string wal_path_string = wal_path.string();
        db->wal_writer_ = std::make_unique<WalWriter>(wal_path_string, sync_mode, sync_interval);
        if(!db->wal_writer_->isOpen())
            return nullptr;
        
        // replay the WAL
        WalReader wal_reader(wal_path_string);
        if(!wal_reader.isOpen())
            return nullptr;
        WalReplayResult replay_result;
        if(!wal_reader.replay(db->memtable_, replay_result))
            return nullptr;
        
        // recover the next sequence 
        if(replay_result.records_replayed == 0)
            db->next_sequence_ = 1;
        else
            db->next_sequence_ = replay_result.max_sequence + 1;
        db->open_ = true;
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
        return true;
    }
    LookupResult DB::get(std::string_view user_key, std::string& value) const
    {
        if(!open_)
            return LookupResult::kNotFound;
        return memtable_.get(user_key, value);
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
        return true;
    }
}