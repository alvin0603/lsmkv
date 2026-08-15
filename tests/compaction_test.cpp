#include <catch_amalgamated.hpp>
#include <lsmkv/internal_key.h>
#include <lsmkv/memtable.h>
#include <lsmkv/sstable_reader.h>
#include <lsmkv/sstable_writer.h>
#include "internal/compaction.h"

#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("Compaction writes newest entries and keeps tombstones", "[compaction]")
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "lsmkv_compaction_test";
    const std::filesystem::path old_path = directory / "1.sst";
    const std::filesystem::path new_path = directory / "2.sst";
    const std::filesystem::path output_path = directory / "3.sst";
    std::filesystem::remove_all(directory);
    REQUIRE(std::filesystem::create_directory(directory));
    std::string apple_old;
    std::string banana_old;
    std::string cat;
    std::string apple_new;
    std::string banana_delete;
    std::string dog;
    REQUIRE(lsmkv::appendInternalKey(apple_old, "apple", 10, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana_old, "banana", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(cat, "cat", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(apple_new, "apple", 50, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana_delete, "banana", 60, lsmkv::ValueType::kDelete));
    REQUIRE(lsmkv::appendInternalKey(dog, "dog", 40, lsmkv::ValueType::kPut));
    {
        lsmkv::SSTableWriter writer(old_path.string(), 30);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(apple_old, "old"));
        REQUIRE(writer.add(banana_old, "yellow"));
        REQUIRE(writer.add(cat, "black"));
        REQUIRE(writer.finish());
    }
    {
        lsmkv::SSTableWriter writer(new_path.string(), 30);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(apple_new, "new"));
        REQUIRE(writer.add(banana_delete, ""));
        REQUIRE(writer.add(dog, "brown"));
        REQUIRE(writer.finish());
    }
    lsmkv::SSTableReader old_reader(old_path.string());
    lsmkv::SSTableReader new_reader(new_path.string());
    REQUIRE(old_reader.isOpen());
    REQUIRE(new_reader.isOpen());
    std::vector<const lsmkv::SSTableReader*> readers = {&old_reader, &new_reader};
    lsmkv::CompactionOutput output;
    REQUIRE(lsmkv::writeCompactedTable(output_path.string(), readers, output));
    REQUIRE(output.smallest_key == apple_new);
    REQUIRE(output.largest_key == dog);
    lsmkv::SSTableReader output_reader(output_path.string());
    REQUIRE(output_reader.isOpen());
    std::string value;
    REQUIRE(output_reader.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "new");
    REQUIRE(output_reader.get("banana", value) == lsmkv::LookupResult::kDeleted);
    REQUIRE(output_reader.get("cat", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "black");
    REQUIRE(output_reader.get("dog", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "brown");
    std::filesystem::remove_all(directory);
}
