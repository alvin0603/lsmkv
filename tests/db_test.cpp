#include <catch_amalgamated.hpp>
#include <lsmkv/db.h>
#include <lsmkv/manifest.h>
#include <lsmkv/sstable_reader.h>
#include "internal/compaction.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("DB stores and deletes values", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_test";
    std::filesystem::remove_all(path);
    auto db = lsmkv::DB::open(path.string());
    REQUIRE(db != nullptr);
    REQUIRE(db->isOpen());
    REQUIRE(db->put("apple", "old"));
    REQUIRE(db->put("apple", "new"));
    REQUIRE(db->put("banana", "yellow"));
    std::string value;
    REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "new");
    REQUIRE(db->deleteKey("banana"));
    REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kDeleted);
    db->close();
    REQUIRE_FALSE(db->isOpen());
    REQUIRE_FALSE(db->put("cat", "orange"));
    REQUIRE_FALSE(db->deleteKey("apple"));
    REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kNotFound);
    std::filesystem::remove_all(path);
}

TEST_CASE("DB recovers values and sequence numbers from WAL", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_recovery_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "old"));
        REQUIRE(db->put("apple", "new"));
        REQUIRE(db->put("banana", "yellow"));
        REQUIRE(db->deleteKey("banana"));
    }
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        std::string value;
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "new");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kDeleted);
        REQUIRE(db->put("apple", "newest"));
    }
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        std::string value;
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "newest");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kDeleted);
    }
    std::filesystem::remove_all(path);
}

TEST_CASE("DB allows only one instance per directory", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_lock_test";
    std::filesystem::remove_all(path);
    auto first_db = lsmkv::DB::open(path.string());
    REQUIRE(first_db != nullptr);
    auto second_db = lsmkv::DB::open(path.string());
    REQUIRE(second_db == nullptr);
    first_db->close();
    auto third_db = lsmkv::DB::open(path.string());
    REQUIRE(third_db != nullptr);
    third_db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB releases its lock when opening fails", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_failed_open_test";
    std::filesystem::remove_all(path);
    auto invalid_db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryN, 0);
    REQUIRE(invalid_db == nullptr);
    auto valid_db = lsmkv::DB::open(path.string());
    REQUIRE(valid_db != nullptr);
    valid_db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB flushes MemTables and recovers persistent sequence state", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_flush_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "red"));
        REQUIRE(db->deleteKey("banana"));
        REQUIRE(db->flush());
        REQUIRE_FALSE(std::filesystem::exists(path / "1.wal"));
        REQUIRE(std::filesystem::exists(path / "2.wal"));
        REQUIRE(std::filesystem::exists(path / "1.sst"));
        REQUIRE(std::filesystem::exists(path / "MANIFEST"));
    }
    lsmkv::ManifestState first_manifest;
    REQUIRE(lsmkv::loadManifest(path.string(), first_manifest));
    REQUIRE(first_manifest.next_file_id == 2);
    REQUIRE(first_manifest.last_sequence == 2);
    REQUIRE(first_manifest.durable_wal_epoch == 1);
    REQUIRE(first_manifest.tables.size() == 1);
    REQUIRE(first_manifest.tables[0].file_id == 1);
    REQUIRE(first_manifest.tables[0].level == 0);
    REQUIRE(first_manifest.tables[0].file_size == std::filesystem::file_size(path / "1.sst"));
    {
        lsmkv::SSTableReader reader((path / "1.sst").string());
        REQUIRE(reader.isOpen());
        std::string value;
        REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "red");
        REQUIRE(reader.get("banana", value) == lsmkv::LookupResult::kDeleted);
    }
    {
        std::ofstream obsolete_wal(path / "1.wal", std::ios::binary);
        obsolete_wal << "obsolete";
        REQUIRE(obsolete_wal.good());
    }

    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        REQUIRE_FALSE(std::filesystem::exists(path / "1.wal"));
        REQUIRE(db->put("cat", "black"));
        REQUIRE(db->flush());
        REQUIRE_FALSE(std::filesystem::exists(path / "2.wal"));
        REQUIRE(std::filesystem::exists(path / "3.wal"));
        REQUIRE(std::filesystem::exists(path / "2.sst"));
    }

    lsmkv::ManifestState second_manifest;
    REQUIRE(lsmkv::loadManifest(path.string(), second_manifest));
    REQUIRE(second_manifest.next_file_id == 3);
    REQUIRE(second_manifest.last_sequence == 3);
    REQUIRE(second_manifest.durable_wal_epoch == 2);
    REQUIRE(second_manifest.tables.size() == 2);
    REQUIRE(second_manifest.tables[1].file_id == 2);
    {
        lsmkv::SSTableReader reader((path / "2.sst").string());
        REQUIRE(reader.isOpen());
        std::string value;
        REQUIRE(reader.get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "black");
    }
    std::filesystem::remove_all(path);
}

TEST_CASE("DB removes an orphan SSTable before reusing its file id", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_orphan_sstable_test";
    std::filesystem::remove_all(path);
    REQUIRE(std::filesystem::create_directory(path));
    {
        std::ofstream orphan_table(path / "1.sst", std::ios::binary);
        orphan_table << "uncommitted";
        REQUIRE(orphan_table.good());
    }
    auto db = lsmkv::DB::open(path.string());
    REQUIRE(db != nullptr);
    REQUIRE_FALSE(std::filesystem::exists(path / "1.sst"));
    REQUIRE(db->put("apple", "red"));
    REQUIRE(db->flush());
    REQUIRE(std::filesystem::exists(path / "1.sst"));
    lsmkv::SSTableReader reader((path / "1.sst").string());
    REQUIRE(reader.isOpen());
    std::string value;
    REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB flushes automatically at the MemTable size limit", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_automatic_flush_test";
    std::filesystem::remove_all(path);
    auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, 1);
    REQUIRE(db != nullptr);
    REQUIRE(db->put("apple", "red"));
    REQUIRE(std::filesystem::exists(path / "1.sst"));
    REQUIRE_FALSE(std::filesystem::exists(path / "1.wal"));
    REQUIRE(std::filesystem::exists(path / "2.wal"));
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 2);
    REQUIRE(state.last_sequence == 1);
    REQUIRE(state.durable_wal_epoch == 1);
    REQUIRE(state.tables.size() == 1);
    db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB flushes a large recovered MemTable during open", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_recovery_flush_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, 1024);
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "red"));
        REQUIRE_FALSE(std::filesystem::exists(path / "1.sst"));
    }
    auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, 1);
    REQUIRE(db != nullptr);
    REQUIRE(std::filesystem::exists(path / "1.sst"));
    REQUIRE(std::filesystem::exists(path / "MANIFEST"));
    REQUIRE_FALSE(std::filesystem::exists(path / "1.wal"));
    REQUIRE(std::filesystem::exists(path / "2.wal"));
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 2);
    REQUIRE(state.last_sequence == 1);
    REQUIRE(state.durable_wal_epoch == 1);
    REQUIRE(state.tables.size() == 1);
    lsmkv::SSTableReader reader((path / "1.sst").string());
    REQUIRE(reader.isOpen());
    std::string value;
    REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB closes after a flush failure and recovers from WAL", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_flush_failure_test";
    std::filesystem::remove_all(path);
    auto db = lsmkv::DB::open(path.string());
    REQUIRE(db != nullptr);
    REQUIRE(db->put("apple", "red"));
    {
        std::ofstream conflicting_table(path / "1.sst", std::ios::binary);
        conflicting_table << "conflict";
        REQUIRE(conflicting_table.good());
    }
    REQUIRE_FALSE(db->flush());
    REQUIRE_FALSE(db->isOpen());
    REQUIRE_FALSE(db->flush());
    db.reset();
    db = lsmkv::DB::open(path.string());
    REQUIRE(db != nullptr);
    std::string value;
    REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    REQUIRE(db->put("banana", "yellow"));
    REQUIRE(db->flush());
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 2);
    REQUIRE(state.last_sequence == 2);
    REQUIRE(state.durable_wal_epoch == 2);
    REQUIRE(state.tables.size() == 1);
    lsmkv::SSTableReader reader((path / "1.sst").string());
    REQUIRE(reader.isOpen());
    REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    REQUIRE(reader.get("banana", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "yellow");
    db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB reads the newest result across MemTables and L0 SSTables", "[db]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_multiple_sources_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "old"));
        REQUIRE(db->put("banana", "yellow"));
        REQUIRE(db->put("cat", "black"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "new"));
        REQUIRE(db->deleteKey("banana"));
        std::string value;
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kDeleted);
        REQUIRE(db->flush());
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "new");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kDeleted);
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "black");
        REQUIRE(db->put("apple", "newest"));
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "newest");
        REQUIRE(db->get("dog", value) == lsmkv::LookupResult::kNotFound);
        REQUIRE(db->flush());
    }
    auto db = lsmkv::DB::open(path.string());
    REQUIRE(db != nullptr);
    std::string value;
    REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "newest");
    REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kDeleted);
    REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "black");
    REQUIRE(db->get("dog", value) == lsmkv::LookupResult::kNotFound);
    db->close();
    std::filesystem::remove_all(path);
}

TEST_CASE("DB reports shared block cache statistics", "[db][block-cache]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_block_cache_test";
    std::filesystem::remove_all(path);
    auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, lsmkv::kDefaultMemTableSize, lsmkv::kDefaultL0CompactionTrigger, 1024);
    REQUIRE(db != nullptr);
    REQUIRE(db->put("apple", "red"));
    REQUIRE(db->put("banana", "yellow"));
    REQUIRE(db->flush());
    REQUIRE(db->blockCacheHitCount() == 0);
    REQUIRE(db->blockCacheMissCount() == 0);
    REQUIRE(db->blockCacheHitRate() == 0.0);
    std::string value;
    REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    REQUIRE(db->blockCacheHitCount() == 0);
    REQUIRE(db->blockCacheMissCount() == 1);
    REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "yellow");
    REQUIRE(db->blockCacheHitCount() == 1);
    REQUIRE(db->blockCacheMissCount() == 1);
    REQUIRE(db->blockCacheHitRate() == 0.5);
    REQUIRE(db->get("missing", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(db->blockCacheHitCount() == 1);
    REQUIRE(db->blockCacheMissCount() == 1);
    db->close();
    REQUIRE(db->blockCacheHitRate() == 0.5);
    std::filesystem::remove_all(path);
}

TEST_CASE("DB compacts L0 SSTables into L1", "[db][compaction]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_compaction_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, lsmkv::kDefaultMemTableSize, 2);
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "old"));
        REQUIRE(db->put("banana", "yellow"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "new"));
        REQUIRE(db->put("cat", "black"));
        REQUIRE(db->flush());
        REQUIRE_FALSE(std::filesystem::exists(path / "1.sst"));
        REQUIRE_FALSE(std::filesystem::exists(path / "2.sst"));
        REQUIRE(std::filesystem::exists(path / "3.sst"));
        std::string value;
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "new");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "yellow");
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "black");
    }
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 4);
    REQUIRE(state.last_sequence == 4);
    REQUIRE(state.durable_wal_epoch == 2);
    REQUIRE(state.tables.size() == 1);
    REQUIRE(state.tables[0].file_id == 3);
    REQUIRE(state.tables[0].level == 1);
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        std::string value;
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "new");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "yellow");
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "black");
    }
    std::filesystem::remove_all(path);
}

TEST_CASE("DB compaction keeps tombstones across L1 SSTables", "[db][compaction]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_compaction_tombstone_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, lsmkv::kDefaultMemTableSize, 2);
        REQUIRE(db != nullptr);
        REQUIRE(db->put("cat", "old"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "red"));
        REQUIRE(db->flush());
        REQUIRE(std::filesystem::exists(path / "3.sst"));
        REQUIRE(db->deleteKey("cat"));
        REQUIRE(db->flush());
        REQUIRE(db->put("dog", "brown"));
        REQUIRE(db->flush());
        REQUIRE(std::filesystem::exists(path / "6.sst"));
        std::string value;
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kDeleted);
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "red");
        REQUIRE(db->get("dog", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "brown");
    }
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 7);
    REQUIRE(state.tables.size() == 2);
    REQUIRE(state.tables[0].file_id == 3);
    REQUIRE(state.tables[0].level == 1);
    REQUIRE(state.tables[1].file_id == 6);
    REQUIRE(state.tables[1].level == 1);
    {
        lsmkv::SSTableReader old_reader((path / "3.sst").string());
        REQUIRE(old_reader.isOpen());
        std::string value;
        REQUIRE(old_reader.get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "old");
        lsmkv::SSTableReader new_reader((path / "6.sst").string());
        REQUIRE(new_reader.isOpen());
        REQUIRE(new_reader.get("cat", value) == lsmkv::LookupResult::kDeleted);
    }
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        std::string value;
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kDeleted);
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "red");
        REQUIRE(db->get("dog", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "brown");
    }
    std::filesystem::remove_all(path);
}

TEST_CASE("DB recovers when compaction output was not committed", "[db][compaction]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_compaction_before_manifest_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, lsmkv::kDefaultMemTableSize, 3);
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "old"));
        REQUIRE(db->put("banana", "yellow"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "new"));
        REQUIRE(db->put("cat", "black"));
        REQUIRE(db->flush());
    }
    {
        lsmkv::SSTableReader old_reader((path / "1.sst").string());
        REQUIRE(old_reader.isOpen());
        std::vector<const lsmkv::SSTableReader*> readers = {&old_reader};
        lsmkv::CompactionOutput output;
        REQUIRE(lsmkv::writeCompactedTable((path / "3.sst").string(), readers, output));
    }
    REQUIRE(std::filesystem::exists(path / "1.sst"));
    REQUIRE(std::filesystem::exists(path / "2.sst"));
    REQUIRE(std::filesystem::exists(path / "3.sst"));
    {
        auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, lsmkv::kDefaultMemTableSize, 2);
        REQUIRE(db != nullptr);
        REQUIRE_FALSE(std::filesystem::exists(path / "1.sst"));
        REQUIRE_FALSE(std::filesystem::exists(path / "2.sst"));
        REQUIRE(std::filesystem::exists(path / "3.sst"));
        std::string value;
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "new");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "yellow");
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "black");
    }
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    REQUIRE(state.next_file_id == 4);
    REQUIRE(state.tables.size() == 1);
    REQUIRE(state.tables[0].file_id == 3);
    REQUIRE(state.tables[0].level == 1);
    std::filesystem::remove_all(path);
}

TEST_CASE("DB removes obsolete inputs after committed compaction", "[db][compaction]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_compaction_after_manifest_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string(), lsmkv::SyncMode::kSyncEveryWrite, 1, lsmkv::kDefaultMemTableSize, 3);
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "old"));
        REQUIRE(db->put("banana", "yellow"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "new"));
        REQUIRE(db->put("cat", "black"));
        REQUIRE(db->flush());
    }
    lsmkv::ManifestState state;
    REQUIRE(lsmkv::loadManifest(path.string(), state));
    {
        lsmkv::SSTableReader old_reader((path / "1.sst").string());
        lsmkv::SSTableReader new_reader((path / "2.sst").string());
        REQUIRE(old_reader.isOpen());
        REQUIRE(new_reader.isOpen());
        std::vector<const lsmkv::SSTableReader*> readers = {&old_reader, &new_reader};
        lsmkv::CompactionOutput output;
        REQUIRE(lsmkv::writeCompactedTable((path / "3.sst").string(), readers, output));
        lsmkv::TableMetaData table_metadata;
        table_metadata.file_id = 3;
        table_metadata.level = 1;
        table_metadata.file_size = std::filesystem::file_size(path / "3.sst");
        table_metadata.smallest_key = std::move(output.smallest_key);
        table_metadata.largest_key = std::move(output.largest_key);
        state.next_file_id = 4;
        state.tables.clear();
        state.tables.push_back(std::move(table_metadata));
        REQUIRE(lsmkv::saveManifest(path.string(), state));
    }
    REQUIRE(std::filesystem::exists(path / "1.sst"));
    REQUIRE(std::filesystem::exists(path / "2.sst"));
    REQUIRE(std::filesystem::exists(path / "3.sst"));
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        REQUIRE_FALSE(std::filesystem::exists(path / "1.sst"));
        REQUIRE_FALSE(std::filesystem::exists(path / "2.sst"));
        REQUIRE(std::filesystem::exists(path / "3.sst"));
        std::string value;
        REQUIRE(db->get("apple", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "new");
        REQUIRE(db->get("banana", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "yellow");
        REQUIRE(db->get("cat", value) == lsmkv::LookupResult::kFound);
        REQUIRE(value == "black");
    }
    std::filesystem::remove_all(path);
}

TEST_CASE("DBIterator supports an empty and closed DB", "[db-iterator]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_empty_db_iterator_test";
    std::filesystem::remove_all(path);
    auto db = lsmkv::DB::open(path.string());
    REQUIRE(db != nullptr);
    auto iterator = db->newIterator();
    REQUIRE(iterator != nullptr);
    REQUIRE(iterator->ok());
    REQUIRE_FALSE(iterator->valid());
    REQUIRE(iterator->key().empty());
    REQUIRE(iterator->value().empty());
    iterator.reset();
    db->close();
    REQUIRE(db->newIterator() == nullptr);
    std::filesystem::remove_all(path);
}

TEST_CASE("DBIterator merges MemTables and L0 SSTables", "[db-iterator]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_db_iterator_test";
    std::filesystem::remove_all(path);
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        REQUIRE(db->put("apple", "old"));
        REQUIRE(db->put("banana", "yellow"));
        REQUIRE(db->put("dog", "brown"));
        REQUIRE(db->put("fig", "old"));
        REQUIRE(db->put("fig", "new"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "new"));
        REQUIRE(db->deleteKey("banana"));
        REQUIRE(db->put("cat", "black"));
        REQUIRE(db->put("grape", "green"));
        REQUIRE(db->flush());
        REQUIRE(db->put("apple", "newest"));
        REQUIRE(db->deleteKey("dog"));
        REQUIRE(db->put("eel", "white"));
        auto iterator = db->newIterator();
        REQUIRE(iterator != nullptr);
        REQUIRE(iterator->ok());
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "apple");
        REQUIRE(iterator->value() == "newest");
        iterator->next();
        REQUIRE(iterator->ok());
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "cat");
        REQUIRE(iterator->value() == "black");
        iterator->next();
        REQUIRE(iterator->ok());
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "eel");
        REQUIRE(iterator->value() == "white");
        iterator->next();
        REQUIRE(iterator->ok());
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "fig");
        REQUIRE(iterator->value() == "new");
        iterator->next();
        REQUIRE(iterator->ok());
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "grape");
        REQUIRE(iterator->value() == "green");
        iterator->next();
        REQUIRE(iterator->ok());
        REQUIRE_FALSE(iterator->valid());
    }
    {
        auto db = lsmkv::DB::open(path.string());
        REQUIRE(db != nullptr);
        auto iterator = db->newIterator();
        REQUIRE(iterator != nullptr);
        REQUIRE(iterator->ok());
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "apple");
        REQUIRE(iterator->value() == "newest");
        iterator->next();
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "cat");
        iterator->next();
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "eel");
        iterator->next();
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "fig");
        iterator->next();
        REQUIRE(iterator->valid());
        REQUIRE(iterator->key() == "grape");
        iterator->next();
        REQUIRE(iterator->ok());
        REQUIRE_FALSE(iterator->valid());
    }
    std::filesystem::remove_all(path);
}
