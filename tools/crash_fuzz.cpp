#include "internal/file_io.h"
#include <lsmkv/coding.h>
#include <lsmkv/db.h>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <random>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
    constexpr std::size_t kMemTableSize = 1024;
    constexpr std::size_t kL0CompactionTrigger = 2;
    constexpr std::size_t kValueSize = 512;
    constexpr std::uint64_t kMinDelayMicroseconds = 10 * 1000;
    constexpr std::uint64_t kMaxDelayMicroseconds = 100 * 1000;

    struct Options
    {
        std::size_t iterations = 100;
        std::uint64_t seed = 42;
        bool show_help = false;
    };
    template<typename T>
    bool parseUnsigned(std::string_view input, T& value)
    {
        if(input.empty())
            return false;
        T parsed = 0;
        const auto result = std::from_chars(input.data(), input.data() + input.size(), parsed);
        if(result.ec != std::errc{} || result.ptr != input.data() + input.size())
            return false;
        value = parsed;
        return true;
    }
    void printUsage()
    {
        std::cout << "usage: lsmkv_crash_fuzz [options]\n";
        std::cout << "  --iterations <count>\n";
        std::cout << "  --seed <value>\n";
    }
    bool parseArguments(int argc, char* argv[], Options& options)
    {
        for(int i = 1; i < argc; i++)
        {
            const std::string_view argument = argv[i];
            if(argument == "--help")
            {
                options.show_help = true;
                continue;
            }
            if(i + 1 >= argc)
                return false;
            const std::string_view value = argv[++i];
            if(argument == "--iterations")
            {
                if(!parseUnsigned(value, options.iterations) || options.iterations == 0)
                    return false;
            }
            else if(argument == "--seed")
            {
                if(!parseUnsigned(value, options.seed))
                    return false;
            }
            else
                return false;
        }
        return true;
    }
    std::string makeKey(std::uint64_t operation_id)
    {
        const std::string number = std::to_string(operation_id);
        std::string key = "key_";
        if(number.size() < 20)
            key.append(20 - number.size(), '0');
        key.append(number);
        return key;
    }
    std::string makeValue(std::uint64_t operation_id)
    {
        std::string value = "value_" + std::to_string(operation_id) + "_";
        value.resize(kValueSize, static_cast<char>('a' + operation_id % 26));
        return value;
    }
    bool parseKey(std::string_view key, std::uint64_t& operation_id)
    {
        constexpr std::string_view prefix = "key_";
        if(!key.starts_with(prefix))
            return false;
        key.remove_prefix(prefix.size());
        if(key.empty())
            return false;
        const auto result = std::from_chars(key.data(), key.data() + key.size(), operation_id);
        return result.ec == std::errc{} && result.ptr == key.data() + key.size() && operation_id != 0;
    }
    bool createWorkDirectory(std::filesystem::path& path)
    {
        const std::filesystem::path temporary_directory = std::filesystem::temp_directory_path();
        const std::string prefix = "lsmkv_crash_fuzz_" + std::to_string(::getpid()) + "_";
        for(std::size_t attempt = 0; attempt < 100; attempt++)
        {
            path = temporary_directory / (prefix + std::to_string(attempt));
            std::error_code error;
            if(std::filesystem::create_directory(path, error))
                return true;
            if(error && error != std::errc::file_exists)
                return false;
        }
        return false;
    }
    int runWriter(const std::filesystem::path& db_path, const std::filesystem::path& oracle_path, int ready_fd)
    {
        auto db = lsmkv::DB::open(db_path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, kMemTableSize, kL0CompactionTrigger);
        if(!db)
            return 1;
        const std::string oracle_path_string = oracle_path.string();
        const int oracle_fd = ::open(oracle_path_string.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(oracle_fd == -1)
            return 1;
        if(!lsmkv::writeAll(ready_fd, "R"))
        {
            ::close(oracle_fd);
            return 1;
        }
        ::close(ready_fd);
        std::uint64_t operation_id = 1;
        while(operation_id != std::numeric_limits<std::uint64_t>::max())
        {
            if(!db->put(makeKey(operation_id), makeValue(operation_id)))
            {
                ::close(oracle_fd);
                return 1;
            }
            std::string record;
            lsmkv::appendFixed64(record, operation_id);
            if(!lsmkv::writeAll(oracle_fd, record) || !lsmkv::syncFile(oracle_fd))
            {
                ::close(oracle_fd);
                return 1;
            }
            operation_id++;
        }
        ::close(oracle_fd);
        return 1;
    }
    bool waitForProcess(pid_t process_id, int& status)
    {
        while(::waitpid(process_id, &status, 0) == -1)
        {
            if(errno == EINTR)
                continue;
            return false;
        }
        return true;
    }
    bool readOracle(const std::filesystem::path& path, std::uint64_t& last_operation_id, std::string& error_message)
    {
        const std::string path_string = path.string();
        const int fd = ::open(path_string.c_str(), O_RDONLY);
        if(fd == -1)
        {
            error_message = "failed to open oracle";
            return false;
        }
        struct stat file_stat;
        if(::fstat(fd, &file_stat) == -1 || file_stat.st_size < 0)
        {
            ::close(fd);
            error_message = "failed to read oracle size";
            return false;
        }
        const std::uint64_t record_count = static_cast<std::uint64_t>(file_stat.st_size) / sizeof(std::uint64_t);
        last_operation_id = 0;
        for(std::uint64_t index = 0; index < record_count; index++)
        {
            std::string record(sizeof(std::uint64_t), '\0');
            if(lsmkv::readExact(fd, record.data(), record.size()) != lsmkv::FileReadResult::kComplete)
            {
                ::close(fd);
                error_message = "failed to read oracle record";
                return false;
            }
            std::string_view input = record;
            std::uint64_t operation_id = 0;
            if(!lsmkv::consumeFixed64(input, operation_id) || !input.empty() || operation_id != last_operation_id + 1)
            {
                ::close(fd);
                error_message = "oracle operation IDs are invalid";
                return false;
            }
            last_operation_id = operation_id;
        }
        ::close(fd);
        return true;
    }
    bool verifyDatabase(const std::filesystem::path& db_path, std::uint64_t last_operation_id, std::string& error_message)
    {
        auto db = lsmkv::DB::open(db_path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, kMemTableSize, kL0CompactionTrigger);
        if(!db)
        {
            error_message = "recovery failed to open the DB";
            return false;
        }
        for(std::uint64_t operation_id = 1; operation_id <= last_operation_id; operation_id++)
        {
            std::string value;
            if(db->get(makeKey(operation_id), value) != lsmkv::LookupResult::kFound || value != makeValue(operation_id))
            {
                error_message = "acknowledged operation " + std::to_string(operation_id) + " is missing or incorrect";
                return false;
            }
        }
        auto iterator = db->newIterator();
        if(!iterator)
        {
            error_message = "failed to create recovery iterator";
            return false;
        }
        std::uint64_t previous_operation_id = 0;
        std::uint64_t visible_entry_count = 0;
        while(iterator->valid())
        {
            std::uint64_t operation_id = 0;
            if(!parseKey(iterator->key(), operation_id) || operation_id <= previous_operation_id || operation_id > last_operation_id + 1 || iterator->value() != makeValue(operation_id))
            {
                error_message = "recovery iterator returned invalid data";
                return false;
            }
            previous_operation_id = operation_id;
            visible_entry_count++;
            iterator->next();
        }
        if(!iterator->ok())
        {
            error_message = "recovery iterator failed";
            return false;
        }
        if(visible_entry_count < last_operation_id || visible_entry_count > last_operation_id + 1)
        {
            error_message = "recovery returned an invalid number of entries";
            return false;
        }
        db->close();
        return true;
    }
    bool runIteration(const std::filesystem::path& work_directory, std::size_t iteration, std::uint64_t delay_microseconds, std::uint64_t& acknowledged_operations, std::string& error_message)
    {
        const std::filesystem::path iteration_path = work_directory / ("iteration_" + std::to_string(iteration));
        const std::filesystem::path db_path = iteration_path / "db";
        const std::filesystem::path oracle_path = iteration_path / "oracle";
        std::error_code error;
        if(!std::filesystem::create_directory(iteration_path, error) || error)
        {
            error_message = "failed to create iteration directory";
            return false;
        }
        int ready_pipe[2];
        if(::pipe(ready_pipe) == -1)
        {
            error_message = "failed to create ready pipe";
            return false;
        }
        const pid_t child_id = ::fork();
        if(child_id == -1)
        {
            ::close(ready_pipe[0]);
            ::close(ready_pipe[1]);
            error_message = "failed to fork writer child";
            return false;
        }
        if(child_id == 0)
        {
            ::close(ready_pipe[0]);
            const int result = runWriter(db_path, oracle_path, ready_pipe[1]);
            ::_exit(result);
        }
        ::close(ready_pipe[1]);
        char ready = '\0';
        const lsmkv::FileReadResult ready_result = lsmkv::readExact(ready_pipe[0], &ready, 1);
        ::close(ready_pipe[0]);
        if(ready_result != lsmkv::FileReadResult::kComplete || ready != 'R')
        {
            ::kill(child_id, SIGKILL);
            int status = 0;
            waitForProcess(child_id, status);
            error_message = "writer child failed before it became ready";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(delay_microseconds));
        if(::kill(child_id, SIGKILL) == -1)
        {
            int status = 0;
            waitForProcess(child_id, status);
            error_message = "writer child exited before SIGKILL";
            return false;
        }
        int status = 0;
        if(!waitForProcess(child_id, status) || !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
        {
            error_message = "writer child did not terminate with SIGKILL";
            return false;
        }
        if(!readOracle(oracle_path, acknowledged_operations, error_message))
            return false;
        if(!verifyDatabase(db_path, acknowledged_operations, error_message))
            return false;
        std::filesystem::remove_all(iteration_path, error);
        if(error)
        {
            error_message = "failed to remove completed iteration directory";
            return false;
        }
        return true;
    }
}

int main(int argc, char* argv[])
{
    Options options;
    if(!parseArguments(argc, argv, options))
    {
        printUsage();
        return 1;
    }
    if(options.show_help)
    {
        printUsage();
        return 0;
    }
    std::filesystem::path work_directory;
    if(!createWorkDirectory(work_directory))
    {
        std::cerr << "failed to create work directory\n";
        return 1;
    }
    std::mt19937_64 delay_generator(options.seed);
    std::uniform_int_distribution<std::uint64_t> delay_distribution(kMinDelayMicroseconds, kMaxDelayMicroseconds);
    std::uint64_t total_acknowledged_operations = 0;
    for(std::size_t iteration = 1; iteration <= options.iterations; iteration++)
    {
        const std::uint64_t delay_microseconds = delay_distribution(delay_generator);
        std::uint64_t acknowledged_operations = 0;
        std::string error_message;
        if(!runIteration(work_directory, iteration, delay_microseconds, acknowledged_operations, error_message))
        {
            std::cerr << "iteration " << iteration << " failed: " << error_message << '\n';
            std::cerr << "seed: " << options.seed << '\n';
            std::cerr << "delay_us: " << delay_microseconds << '\n';
            std::cerr << "work directory: " << work_directory << '\n';
            return 1;
        }
        total_acknowledged_operations += acknowledged_operations;
    }
    std::error_code error;
    std::filesystem::remove_all(work_directory, error);
    if(error)
    {
        std::cerr << "failed to remove work directory: " << work_directory << '\n';
        return 1;
    }
    std::cout << options.iterations << " crash iterations passed\n";
    std::cout << total_acknowledged_operations << " acknowledged operations verified\n";
    return 0;
}
