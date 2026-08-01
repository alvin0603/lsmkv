#include <catch_amalgamated.hpp>
#include <lsmkv/db.h>

#include <filesystem>
#include <string>

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
