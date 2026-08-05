#include <catch_amalgamated.hpp>
#include <lsmkv/internal_key.h>
#include <lsmkv/manifest.h>

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("Manifest encodes and decodes its complete state", "[manifest]")
{
    lsmkv::ManifestState state;
    state.next_file_id = 9;
    state.last_sequence = 81;
    state.durable_wal_epoch = 7;
    lsmkv::TableMetaData first_table;
    first_table.file_id = 3;
    first_table.level = 0;
    first_table.file_size = 1024;
    REQUIRE(lsmkv::appendInternalKey(first_table.smallest_key, "apple", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(first_table.largest_key, "cat", 10, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData second_table;
    second_table.file_id = 8;
    second_table.level = 1;
    second_table.file_size = 2048;
    REQUIRE(lsmkv::appendInternalKey(second_table.smallest_key, "dog", 20, lsmkv::ValueType::kDelete));
    REQUIRE(lsmkv::appendInternalKey(second_table.largest_key, "zebra", 5, lsmkv::ValueType::kPut));
    state.tables.push_back(first_table);
    state.tables.push_back(second_table);
    std::string encoded_manifest;
    REQUIRE(lsmkv::encodeManifest(state, encoded_manifest));
    lsmkv::ManifestState decoded_state;
    REQUIRE(lsmkv::decodeManifest(encoded_manifest, decoded_state));
    REQUIRE(decoded_state.next_file_id == 9);
    REQUIRE(decoded_state.last_sequence == 81);
    REQUIRE(decoded_state.durable_wal_epoch == 7);
    REQUIRE(decoded_state.tables.size() == 2);
    REQUIRE(decoded_state.tables[0].file_id == 3);
    REQUIRE(decoded_state.tables[0].level == 0);
    REQUIRE(decoded_state.tables[0].file_size == 1024);
    REQUIRE(decoded_state.tables[0].smallest_key == first_table.smallest_key);
    REQUIRE(decoded_state.tables[0].largest_key == first_table.largest_key);
    REQUIRE(decoded_state.tables[1].file_id == 8);
    REQUIRE(decoded_state.tables[1].level == 1);
    REQUIRE(decoded_state.tables[1].file_size == 2048);
    REQUIRE(decoded_state.tables[1].smallest_key == second_table.smallest_key);
    REQUIRE(decoded_state.tables[1].largest_key == second_table.largest_key);
}

TEST_CASE("Manifest rejects damaged and invalid states", "[manifest]")
{
    lsmkv::ManifestState state;
    state.next_file_id = 2;
    lsmkv::TableMetaData table;
    table.file_id = 1;
    table.level = 0;
    table.file_size = 512;
    REQUIRE(lsmkv::appendInternalKey(table.smallest_key, "apple", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(table.largest_key, "banana", 10, lsmkv::ValueType::kPut));
    state.tables.push_back(table);
    std::string encoded_manifest;
    REQUIRE(lsmkv::encodeManifest(state, encoded_manifest));
    lsmkv::ManifestState decoded_state;
    std::string truncated_manifest = encoded_manifest.substr(0, encoded_manifest.size() - 1);
    REQUIRE_FALSE(lsmkv::decodeManifest(truncated_manifest, decoded_state));
    std::string trailing_manifest = encoded_manifest;
    trailing_manifest.push_back('\0');
    REQUIRE_FALSE(lsmkv::decodeManifest(trailing_manifest, decoded_state));
    encoded_manifest[0] ^= 1;
    REQUIRE_FALSE(lsmkv::decodeManifest(encoded_manifest, decoded_state));
    state.next_file_id = 1;
    REQUIRE_FALSE(lsmkv::encodeManifest(state, encoded_manifest));
}

TEST_CASE("Manifest saves and replaces a snapshot atomically", "[manifest]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_manifest_test";
    std::filesystem::remove_all(path);
    REQUIRE(std::filesystem::create_directory(path));
    lsmkv::ManifestState first_state;
    first_state.next_file_id = 2;
    first_state.last_sequence = 30;
    first_state.durable_wal_epoch = 1;
    lsmkv::TableMetaData table;
    table.file_id = 1;
    table.level = 0;
    table.file_size = 600;
    REQUIRE(lsmkv::appendInternalKey(table.smallest_key, "apple", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(table.largest_key, "cat", 10, lsmkv::ValueType::kDelete));
    first_state.tables.push_back(table);
    REQUIRE(lsmkv::saveManifest(path.string(), first_state));
    REQUIRE(std::filesystem::exists(path / "MANIFEST"));
    REQUIRE_FALSE(std::filesystem::exists(path / "MANIFEST.tmp"));
    lsmkv::ManifestState second_state = first_state;
    second_state.next_file_id = 3;
    second_state.last_sequence = 45;
    second_state.durable_wal_epoch = 2;
    second_state.tables[0].file_id = 2;
    REQUIRE(lsmkv::saveManifest(path.string(), second_state));
    lsmkv::ManifestState loaded_state;
    REQUIRE(lsmkv::loadManifest(path.string(), loaded_state));
    REQUIRE(loaded_state.next_file_id == 3);
    REQUIRE(loaded_state.last_sequence == 45);
    REQUIRE(loaded_state.durable_wal_epoch == 2);
    REQUIRE(loaded_state.tables.size() == 1);
    REQUIRE(loaded_state.tables[0].file_id == 2);
    REQUIRE_FALSE(std::filesystem::exists(path / "MANIFEST.tmp"));
    std::filesystem::remove_all(path);
}

TEST_CASE("Manifest loads a default state for a new database", "[manifest]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_manifest_new_database_test";
    std::filesystem::remove_all(path);
    REQUIRE(std::filesystem::create_directory(path));
    {
        std::ofstream temporary_manifest(path / "MANIFEST.tmp", std::ios::binary);
        temporary_manifest << "uncommitted";
        REQUIRE(temporary_manifest.good());
    }
    lsmkv::ManifestState state;
    state.next_file_id = 99;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 1);
    REQUIRE(state.last_sequence == 0);
    REQUIRE(state.durable_wal_epoch == 0);
    REQUIRE(state.tables.empty());
    std::filesystem::remove_all(path);
}
