#include <catch_amalgamated.hpp>
#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>
#include <lsmkv/memtable.h>
#include <lsmkv/sstable_format.h>
#include <lsmkv/sstable_reader.h>
#include <lsmkv/sstable_writer.h>

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("SSTableReader returns values and tombstones", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_reader_test.sst";
    std::filesystem::remove(path);
    std::string apple_new;
    std::string apple_old;
    std::string banana_delete;
    std::string banana_old;
    std::string cat;
    std::string dog;
    REQUIRE(lsmkv::appendInternalKey(apple_new, "apple", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(apple_old, "apple", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana_delete, "banana", 25, lsmkv::ValueType::kDelete));
    REQUIRE(lsmkv::appendInternalKey(banana_old, "banana", 15, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(cat, "cat", 10, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(dog, "dog", 5, lsmkv::ValueType::kPut));
    {
        lsmkv::SSTableWriter writer(path.string(), 40);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(apple_new, "new"));
        REQUIRE(writer.add(apple_old, "old"));
        REQUIRE(writer.add(banana_delete, ""));
        REQUIRE(writer.add(banana_old, "yellow"));
        REQUIRE(writer.add(cat, "black"));
        REQUIRE(writer.add(dog, "brown"));
        REQUIRE(writer.finish());
    }

    lsmkv::SSTableReader reader(path.string());
    REQUIRE(reader.isOpen());
    REQUIRE(reader.dataBlockReadCount() == 0);
    std::string value = "unchanged";
    REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "new");
    value = "unchanged";
    REQUIRE(reader.get("banana", value) == lsmkv::LookupResult::kDeleted);
    REQUIRE(value == "unchanged");
    REQUIRE(reader.get("cat", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "black");
    REQUIRE(reader.get("dog", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "brown");
    REQUIRE(reader.dataBlockReadCount() == 4);
    std::filesystem::remove(path);
}

TEST_CASE("SSTableReader reports missing keys at every boundary", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_reader_missing_test.sst";
    std::filesystem::remove(path);
    std::string apple;
    std::string banana;
    std::string cat;
    REQUIRE(lsmkv::appendInternalKey(apple, "apple", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana, "banana", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(cat, "cat", 10, lsmkv::ValueType::kPut));
    {
        lsmkv::SSTableWriter writer(path.string(), 20);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(apple, "red"));
        REQUIRE(writer.add(banana, "yellow"));
        REQUIRE(writer.add(cat, "black"));
        REQUIRE(writer.finish());
    }
    lsmkv::SSTableReader reader(path.string());
    REQUIRE(reader.isOpen());
    REQUIRE(reader.dataBlockReadCount() == 0);
    std::string value = "unchanged";
    REQUIRE(reader.get("aardvark", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(value == "unchanged");
    REQUIRE(reader.get("blueberry", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(value == "unchanged");
    REQUIRE(reader.get("zebra", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(value == "unchanged");
    REQUIRE(reader.dataBlockReadCount() == 0);
    std::filesystem::remove(path);
}

TEST_CASE("SSTableReader rejects an invalid magic number", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_reader_magic_test.sst";
    std::filesystem::remove(path);
    std::string key;
    REQUIRE(lsmkv::appendInternalKey(key, "apple", 10, lsmkv::ValueType::kPut));
    {
        lsmkv::SSTableWriter writer(path.string());
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(key, "red"));
        REQUIRE(writer.finish());
    }
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(file.is_open());
        file.seekp(-8, std::ios::end);
        file.put('\0');
        REQUIRE(file.good());
    }

    lsmkv::SSTableReader reader(path.string());
    REQUIRE_FALSE(reader.isOpen());
    std::filesystem::remove(path);
}

TEST_CASE("SSTableReader rejects files smaller than the footer", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_reader_small_file_test.sst";
    std::filesystem::remove(path);
    {
        std::ofstream file(path, std::ios::binary);
        file << "small";
    }
    lsmkv::SSTableReader reader(path.string());
    REQUIRE_FALSE(reader.isOpen());
    std::filesystem::remove(path);
}

TEST_CASE("SSTableReader supports an empty SSTable", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_reader_empty_test.sst";
    std::filesystem::remove(path);
    {
        lsmkv::SSTableWriter writer(path.string());
        REQUIRE(writer.isOpen());
        REQUIRE(writer.finish());
    }
    lsmkv::SSTableReader reader(path.string());
    REQUIRE(reader.isOpen());
    std::string value = "unchanged";
    REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kNotFound);
    REQUIRE(value == "unchanged");
    std::filesystem::remove(path);
}

TEST_CASE("SSTableReader supports SSTables without a Bloom filter", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_reader_old_format_test.sst";
    std::filesystem::remove(path);
    std::string internal_key;
    REQUIRE(lsmkv::appendInternalKey(internal_key, "apple", 10, lsmkv::ValueType::kPut));
    std::string data_block;
    REQUIRE(lsmkv::appendLengthPrefixedSlice(data_block, internal_key));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(data_block, "red"));
    std::string index_block;
    REQUIRE(lsmkv::appendLengthPrefixedSlice(index_block, internal_key));
    lsmkv::appendFixed64(index_block, 0);
    lsmkv::appendFixed64(index_block, data_block.size());
    std::string footer;
    lsmkv::appendFixed64(footer, data_block.size());
    lsmkv::appendFixed64(footer, index_block.size());
    lsmkv::appendFixed64(footer, 0);
    lsmkv::appendFixed64(footer, 0);
    lsmkv::appendFixed64(footer, lsmkv::kSSTableMagic);
    {
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        file.write(data_block.data(), static_cast<std::streamsize>(data_block.size()));
        file.write(index_block.data(), static_cast<std::streamsize>(index_block.size()));
        file.write(footer.data(), static_cast<std::streamsize>(footer.size()));
        REQUIRE(file.good());
    }
    lsmkv::SSTableReader reader(path.string());
    REQUIRE(reader.isOpen());
    std::string value;
    REQUIRE(reader.get("apple", value) == lsmkv::LookupResult::kFound);
    REQUIRE(value == "red");
    REQUIRE(reader.dataBlockReadCount() == 1);
    std::filesystem::remove(path);
}

TEST_CASE("SSTableIterator reads entries across data blocks", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_iterator_test.sst";
    std::filesystem::remove(path);
    std::string apple_new;
    std::string apple_old;
    std::string banana_delete;
    std::string cat;
    REQUIRE(lsmkv::appendInternalKey(apple_new, "apple", 40, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(apple_old, "apple", 30, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana_delete, "banana", 20, lsmkv::ValueType::kDelete));
    REQUIRE(lsmkv::appendInternalKey(cat, "cat", 10, lsmkv::ValueType::kPut));
    {
        lsmkv::SSTableWriter writer(path.string(), 20);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(apple_new, "new"));
        REQUIRE(writer.add(apple_old, "old"));
        REQUIRE(writer.add(banana_delete, ""));
        REQUIRE(writer.add(cat, "black"));
        REQUIRE(writer.finish());
    }

    lsmkv::SSTableReader reader(path.string());
    REQUIRE(reader.isOpen());
    lsmkv::SSTableIterator iterator = reader.newIterator();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(iterator.internalKey() == apple_new);
    REQUIRE(iterator.value() == "new");
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(iterator.internalKey() == apple_old);
    REQUIRE(iterator.value() == "old");
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(iterator.internalKey() == banana_delete);
    REQUIRE(iterator.value().empty());
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(iterator.internalKey() == cat);
    REQUIRE(iterator.value() == "black");
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE_FALSE(iterator.valid());
    REQUIRE(iterator.internalKey().empty());
    REQUIRE(iterator.value().empty());
    std::filesystem::remove(path);
}

TEST_CASE("SSTableIterator supports an empty SSTable", "[sstable-reader]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_empty_sstable_iterator_test.sst";
    std::filesystem::remove(path);
    {
        lsmkv::SSTableWriter writer(path.string());
        REQUIRE(writer.isOpen());
        REQUIRE(writer.finish());
    }
    lsmkv::SSTableReader reader(path.string());
    REQUIRE(reader.isOpen());
    lsmkv::SSTableIterator iterator = reader.newIterator();
    REQUIRE(iterator.ok());
    REQUIRE_FALSE(iterator.valid());
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE_FALSE(iterator.valid());
    std::filesystem::remove(path);
}
