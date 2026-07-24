#include <catch_amalgamated.hpp>
#include <lsmkv/memtable.h>

#include <cstddef>

TEST_CASE("MemTable tracks approximate memory usage", "[memtable]")
{
    lsmkv::MemTable table;
    REQUIRE(table.approximateMemoryUsage() == 0);
    REQUIRE(table.add(10, lsmkv::ValueType::kPut, "name", "alvin"));
    REQUIRE(table.approximateMemoryUsage() == 17);
    REQUIRE(table.add(11, lsmkv::ValueType::kDelete, "name", ""));
    REQUIRE(table.approximateMemoryUsage() == 29);
}

TEST_CASE("MemTable rejects duplicate internal keys", "[memtable]")
{
    lsmkv::MemTable table;
    REQUIRE(table.add(10, lsmkv::ValueType::kPut, "name", "alvin"));
    const std::size_t usage = table.approximateMemoryUsage();
    REQUIRE_FALSE(table.add(10, lsmkv::ValueType::kPut, "name", "another-value"));
    REQUIRE(table.approximateMemoryUsage() == usage);
}

TEST_CASE("MemTable returns a stored value", "[memtable]")
{
    lsmkv::MemTable table;
    REQUIRE(table.add(10, lsmkv::ValueType::kPut, "name", "alvin"));
    std::string value;
    REQUIRE(table.get("name", value) == lsmkv::LookupResult::kFound);
   REQUIRE(value == "alvin");
}

TEST_CASE("MemTable returns the newest version", "[memtable]")
{
    lsmkv::MemTable table;
    REQUIRE(table.add(10, lsmkv::ValueType::kPut, "name", "old"));
    REQUIRE(table.add(30, lsmkv::ValueType::kPut, "name", "newest"));
    REQUIRE(table.add(20, lsmkv::ValueType::kPut, "name", "middle"));
    std::string value;
    REQUIRE(table.get("name", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "newest");
}

TEST_CASE("MemTable reports the newest tombstone", "[memtable]")
{
    lsmkv::MemTable table;
    REQUIRE(table.add(10, lsmkv::ValueType::kPut, "name", "alvin"));
    REQUIRE(table.add(20, lsmkv::ValueType::kDelete, "name", ""));
    std::string value = "unchanged";
    REQUIRE(table.get("name", value) == lsmkv::LookupResult::kDeleted);
    REQUIRE(value == "unchanged");
}

TEST_CASE("MemTable reports missing user keys", "[memtable]")
{
    lsmkv::MemTable table;
    REQUIRE(table.add(10, lsmkv::ValueType::kPut, "banana", "yellow"));
    std::string value = "unchanged";
    REQUIRE(table.get("apple", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(value == "unchanged");
    REQUIRE(table.get("zebra", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(value == "unchanged");
}