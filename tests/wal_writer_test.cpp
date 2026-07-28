#include <catch_amalgamated.hpp>
#include <lsmkv/coding.h>
#include <lsmkv/wal_writer.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("WalWriter appends records in WAL format", "[wal-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_writer_test.wal";
    std::filesystem::remove(path);
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncEveryWrite);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(7, lsmkv::ValueType::kPut, "cat", "red"));
        REQUIRE(writer.append(8, lsmkv::ValueType::kDelete, "cat", ""));
    }
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    const std::string file_data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::string first_payload;
    first_payload.push_back(static_cast<char>(lsmkv::ValueType::kPut));
    lsmkv::appendFixed64(first_payload, 7);
    REQUIRE(lsmkv::appendLengthPrefixedSlice(first_payload, "cat"));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(first_payload, "red"));
    std::string second_payload;
    second_payload.push_back(static_cast<char>(lsmkv::ValueType::kDelete));
    lsmkv::appendFixed64(second_payload, 8);
    REQUIRE(lsmkv::appendLengthPrefixedSlice(second_payload, "cat"));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(second_payload, ""));
    std::string expected;
    lsmkv::appendFixed32(expected, lsmkv::calculateCrc32(first_payload));
    lsmkv::appendFixed32(expected, static_cast<std::uint32_t>(first_payload.size()));
    expected.append(first_payload);
    lsmkv::appendFixed32(expected, lsmkv::calculateCrc32(second_payload));
    lsmkv::appendFixed32(expected, static_cast<std::uint32_t>(second_payload.size()));
    expected.append(second_payload);
    REQUIRE(file_data == expected);
    file.close();
    std::filesystem::remove(path);
}

TEST_CASE("WalWriter syncs every configured number of writes", "[wal-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_writer_every_n_test.wal";
    std::filesystem::remove(path);
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncEveryN, 2);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.append(1, lsmkv::ValueType::kPut, "first", "one"));
        REQUIRE(writer.append(2, lsmkv::ValueType::kPut, "second", "two"));
    }
    REQUIRE(std::filesystem::file_size(path) > 0);
    std::filesystem::remove(path);
}

TEST_CASE("WalWriter rejects zero sync interval", "[wal-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_writer_invalid_interval_test.wal";
    std::filesystem::remove(path);
    lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncEveryN, 0);
    REQUIRE_FALSE(writer.isOpen());
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("WalWriter rejects sequence overflow", "[wal-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_wal_writer_sequence_overflow_test.wal";
    std::filesystem::remove(path);
    {
        lsmkv::WalWriter writer(path.string(), lsmkv::SyncMode::kSyncOff);
        REQUIRE(writer.isOpen());
        REQUIRE_FALSE(writer.append(lsmkv::kMaxSequenceNumber + 1, lsmkv::ValueType::kPut, "key", "value"));
    }
    REQUIRE(std::filesystem::file_size(path) == 0);
    std::filesystem::remove(path);
}
