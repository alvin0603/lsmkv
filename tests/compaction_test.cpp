#include <catch_amalgamated.hpp>
#include <lsmkv/internal_key.h>
#include <lsmkv/memtable.h>
#include <lsmkv/sstable_reader.h>
#include <lsmkv/sstable_writer.h>
#include "internal/compaction.h"
#include "internal/compaction_policy.h"

#include <filesystem>
#include <string>
#include <vector>

TEST_CASE("Tiered compaction policy waits for the L0 trigger", "[compaction]")
{
    lsmkv::TableMetaData l0_table;
    l0_table.file_id = 3;
    l0_table.level = 0;
    l0_table.file_size = 100;
    lsmkv::TableMetaData l1_table;
    l1_table.file_id = 5;
    l1_table.level = 1;
    l1_table.file_size = 200;
    std::vector<lsmkv::TableMetaData> tables = {l0_table, l1_table};
    lsmkv::TieredCompactionPolicy policy;
    lsmkv::CompactionPlan plan;
    REQUIRE_FALSE(policy.createPlan(tables, 2, plan));
}

TEST_CASE("Tiered compaction policy selects every L0 table", "[compaction]")
{
    lsmkv::TableMetaData first_l0_table;
    first_l0_table.file_id = 3;
    first_l0_table.level = 0;
    first_l0_table.file_size = 100;
    lsmkv::TableMetaData l1_table;
    l1_table.file_id = 7;
    l1_table.level = 1;
    l1_table.file_size = 200;
    lsmkv::TableMetaData second_l0_table;
    second_l0_table.file_id = 5;
    second_l0_table.level = 0;
    second_l0_table.file_size = 150;
    std::vector<lsmkv::TableMetaData> tables = {first_l0_table, l1_table, second_l0_table};
    lsmkv::TieredCompactionPolicy policy;
    lsmkv::CompactionPlan plan;
    REQUIRE(policy.createPlan(tables, 2, plan));
    REQUIRE(plan.input_file_ids.size() == 2);
    REQUIRE(plan.input_file_ids[0] == 3);
    REQUIRE(plan.input_file_ids[1] == 5);
    REQUIRE(plan.output_level == 1);
}

TEST_CASE("Leveled compaction policy includes overlapping L1 tables", "[compaction]")
{
    lsmkv::TableMetaData first_l0_table;
    first_l0_table.file_id = 10;
    first_l0_table.level = 0;
    first_l0_table.file_size = 100;
    REQUIRE(lsmkv::appendInternalKey(first_l0_table.smallest_key, "apple", 50, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(first_l0_table.largest_key, "mango", 40, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData first_l1_table;
    first_l1_table.file_id = 5;
    first_l1_table.level = 1;
    first_l1_table.file_size = 200;
    REQUIRE(lsmkv::appendInternalKey(first_l1_table.smallest_key, "banana", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(first_l1_table.largest_key, "fish", 20, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData second_l0_table;
    second_l0_table.file_id = 12;
    second_l0_table.level = 0;
    second_l0_table.file_size = 100;
    REQUIRE(lsmkv::appendInternalKey(second_l0_table.smallest_key, "horse", 60, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(second_l0_table.largest_key, "zebra", 55, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData second_l1_table;
    second_l1_table.file_id = 7;
    second_l1_table.level = 1;
    second_l1_table.file_size = 200;
    REQUIRE(lsmkv::appendInternalKey(second_l1_table.smallest_key, "orange", 15, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(second_l1_table.largest_key, "tiger", 10, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData untouched_l1_table;
    untouched_l1_table.file_id = 9;
    untouched_l1_table.level = 1;
    untouched_l1_table.file_size = 200;
    REQUIRE(lsmkv::appendInternalKey(untouched_l1_table.smallest_key, "zoo", 8, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(untouched_l1_table.largest_key, "zoo", 7, lsmkv::ValueType::kPut));
    std::vector<lsmkv::TableMetaData> tables = {first_l0_table, first_l1_table, second_l0_table, second_l1_table, untouched_l1_table};
    lsmkv::LeveledCompactionPolicy policy(1000);
    lsmkv::CompactionPlan plan;
    REQUIRE(policy.createPlan(tables, 2, plan));
    REQUIRE(plan.input_file_ids.size() == 4);
    REQUIRE(plan.input_file_ids[0] == 10);
    REQUIRE(plan.input_file_ids[1] == 12);
    REQUIRE(plan.input_file_ids[2] == 5);
    REQUIRE(plan.input_file_ids[3] == 7);
    REQUIRE(plan.output_level == 1);
}

TEST_CASE("Leveled compaction policy moves an overfull level to the next level", "[compaction]")
{
    lsmkv::TableMetaData first_l2_table;
    first_l2_table.file_id = 20;
    first_l2_table.level = 2;
    first_l2_table.file_size = 6000;
    REQUIRE(lsmkv::appendInternalKey(first_l2_table.smallest_key, "cat", 50, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(first_l2_table.largest_key, "horse", 40, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData second_l2_table;
    second_l2_table.file_id = 21;
    second_l2_table.level = 2;
    second_l2_table.file_size = 5000;
    REQUIRE(lsmkv::appendInternalKey(second_l2_table.smallest_key, "orange", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(second_l2_table.largest_key, "zebra", 20, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData first_l3_table;
    first_l3_table.file_id = 3;
    first_l3_table.level = 3;
    first_l3_table.file_size = 200;
    REQUIRE(lsmkv::appendInternalKey(first_l3_table.smallest_key, "apple", 15, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(first_l3_table.largest_key, "dog", 10, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData second_l3_table;
    second_l3_table.file_id = 6;
    second_l3_table.level = 3;
    second_l3_table.file_size = 200;
    REQUIRE(lsmkv::appendInternalKey(second_l3_table.smallest_key, "fish", 8, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(second_l3_table.largest_key, "mango", 7, lsmkv::ValueType::kPut));
    lsmkv::TableMetaData untouched_l3_table;
    untouched_l3_table.file_id = 8;
    untouched_l3_table.level = 3;
    untouched_l3_table.file_size = 200;
    REQUIRE(lsmkv::appendInternalKey(untouched_l3_table.smallest_key, "pear", 6, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(untouched_l3_table.largest_key, "zoo", 5, lsmkv::ValueType::kPut));
    std::vector<lsmkv::TableMetaData> tables = {first_l2_table, second_l2_table, first_l3_table, second_l3_table, untouched_l3_table};
    lsmkv::LeveledCompactionPolicy policy(1000);
    lsmkv::CompactionPlan plan;
    REQUIRE(policy.createPlan(tables, 2, plan));
    REQUIRE(plan.input_file_ids.size() == 3);
    REQUIRE(plan.input_file_ids[0] == 20);
    REQUIRE(plan.input_file_ids[1] == 3);
    REQUIRE(plan.input_file_ids[2] == 6);
    REQUIRE(plan.output_level == 3);
}

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
