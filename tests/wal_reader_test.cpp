#include <catch_amalgamated.hpp>
#include <lsmkv/coding.h>
#include <lsmkv/wal_reader.h>
#include <lsmkv/wal_writer.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("WalReader replays valid records", "[wal-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_reader_test.wal";
    std::filesystem::remove(path);
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncOff);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(10, lsmkv::ValueType::kPut, "apple", "old"));
        REQUIRE(writer.append(20, lsmkv::ValueType::kPut, "apple", "new"));
        REQUIRE(writer.append(30, lsmkv::ValueType::kDelete, "banana", ""));
    }

    lsmkv::MemTable memtable;
    lsmkv::WalReplayResult result;
    {
        lsmkv::WalReader reader(path.string());
        REQUIRE(reader.isOpen());
        REQUIRE(reader.replay(memtable, result));
    }

    REQUIRE(result.records_replayed == 3);
    REQUIRE(result.max_sequence == 30);
    std::string value;
    REQUIRE(memtable.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "new");
    REQUIRE(memtable.get("banana", value) == lsmkv::LookupResult::kDeleted);
    std::filesystem::remove(path);
}

TEST_CASE("WalReader truncates a torn header", "[wal-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_reader_torn_header_test.wal";
    std::filesystem::remove(path);
    std::uintmax_t valid_size = 0;
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncOff);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(10, lsmkv::ValueType::kPut, "apple", "red"));
        valid_size = std::filesystem::file_size(path);
    }
    {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        file.write("\x01\x02\x03", 3);
        REQUIRE(file.good());
    }

    lsmkv::MemTable memtable;
    lsmkv::WalReplayResult result;
    {
        lsmkv::WalReader reader(path.string());
        REQUIRE(reader.isOpen());
        REQUIRE(reader.replay(memtable, result));
    }

    REQUIRE(result.records_replayed == 1);
    REQUIRE(result.max_sequence == 10);
    REQUIRE(std::filesystem::file_size(path) == valid_size);
    std::filesystem::remove(path);
}

TEST_CASE("WalReader truncates a torn payload", "[wal-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_reader_torn_payload_test.wal";
    std::filesystem::remove(path);
    std::uintmax_t valid_size = 0;
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncOff);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(10, lsmkv::ValueType::kPut, "apple", "red"));
        valid_size = std::filesystem::file_size(path);
        REQUIRE(writer.append(20, lsmkv::ValueType::kPut, "banana", "yellow"));
    }
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 3);

    lsmkv::MemTable memtable;
    lsmkv::WalReplayResult result;
    {
        lsmkv::WalReader reader(path.string());
        REQUIRE(reader.isOpen());
        REQUIRE(reader.replay(memtable, result));
    }

    REQUIRE(result.records_replayed == 1);
    REQUIRE(result.max_sequence == 10);
    std::string value;
    REQUIRE(memtable.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    REQUIRE(memtable.get("banana", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(std::filesystem::file_size(path) == valid_size);
    std::filesystem::remove(path);
}

TEST_CASE("WalReader truncates a record with an invalid CRC", "[wal-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_reader_crc_test.wal";
    std::filesystem::remove(path);
    std::uintmax_t valid_size = 0;
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncOff);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(10, lsmkv::ValueType::kPut, "apple", "red"));
        valid_size = std::filesystem::file_size(path);
        REQUIRE(writer.append(20, lsmkv::ValueType::kPut, "banana", "yellow"));
    }
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        file.put('\x7F');
        REQUIRE(file.good());
    }

    lsmkv::MemTable memtable;
    lsmkv::WalReplayResult result;
    {
        lsmkv::WalReader reader(path.string());
        REQUIRE(reader.isOpen());
        REQUIRE(reader.replay(memtable, result));
    }

    REQUIRE(result.records_replayed == 1);
    REQUIRE(result.max_sequence == 10);
    REQUIRE(std::filesystem::file_size(path) == valid_size);
    std::filesystem::remove(path);
}

TEST_CASE("WalReader rejects an oversized payload", "[wal-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_reader_oversized_payload_test.wal";
    std::filesystem::remove(path);
    std::uintmax_t valid_size = 0;
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncOff);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(10, lsmkv::ValueType::kPut, "apple", "red"));
        valid_size = std::filesystem::file_size(path);
    }
    {
        std::string invalid_header;
        lsmkv::appendFixed32(invalid_header, 0);
        lsmkv::appendFixed32(invalid_header, 64U * 1024U * 1024U + 1U);
        std::ofstream file(path, std::ios::binary | std::ios::app);
        file.write(invalid_header.data(), static_cast<std::streamsize>(invalid_header.size()));
        REQUIRE(file.good());
    }

    lsmkv::MemTable memtable;
    lsmkv::WalReplayResult result;
    {
        lsmkv::WalReader reader(path.string());
        REQUIRE(reader.isOpen());
        REQUIRE(reader.replay(memtable, result));
    }

    REQUIRE(result.records_replayed == 1);
    REQUIRE(result.max_sequence == 10);
    REQUIRE(std::filesystem::file_size(path) == valid_size);
    std::filesystem::remove(path);
}
