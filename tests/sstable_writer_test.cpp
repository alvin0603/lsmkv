#include <catch_amalgamated.hpp>
#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>
#include <lsmkv/sstable_format.h>
#include <lsmkv/sstable_writer.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

TEST_CASE("SSTableWriter writes data blocks index and footer", "[sstable-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_test.sst";
    std::filesystem::remove(path);
    std::string apple_key;
    std::string banana_key;
    std::string cat_key;
    REQUIRE(lsmkv::appendInternalKey(apple_key, "apple", 10, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana_key, "banana", 20, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(cat_key, "cat", 30, lsmkv::ValueType::kDelete));
    std::string apple_entry;
    std::string banana_entry;
    std::string cat_entry;
    REQUIRE(lsmkv::appendLengthPrefixedSlice(apple_entry, apple_key));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(apple_entry, "red"));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(banana_entry, banana_key));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(banana_entry, "yellow"));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(cat_entry, cat_key));
    REQUIRE(lsmkv::appendLengthPrefixedSlice(cat_entry, ""));
    REQUIRE(apple_entry.size() == 18);
    REQUIRE(banana_entry.size() == 22);
    REQUIRE(cat_entry.size() == 13);
    {
        lsmkv::SSTableWriter writer(path.string(), 40);
        REQUIRE(writer.isOpen());
        REQUIRE(writer.add(apple_key, "red"));
        REQUIRE(writer.add(banana_key, "yellow"));
        REQUIRE(writer.add(cat_key, ""));
        REQUIRE(writer.finish());
        REQUIRE_FALSE(writer.add(cat_key, ""));
        REQUIRE_FALSE(writer.finish());
    }
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    const std::string file_data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    REQUIRE(file_data.size() >= lsmkv::kSSTableFooterSize);
    std::string_view footer_input(file_data.data() + file_data.size() - lsmkv::kSSTableFooterSize, lsmkv::kSSTableFooterSize);
    std::uint64_t index_offset = 0;
    std::uint64_t index_size = 0;
    std::uint64_t bloom_offset = 0;
    std::uint64_t bloom_size = 0;
    std::uint64_t magic = 0;
    REQUIRE(lsmkv::consumeFixed64(footer_input, index_offset));
    REQUIRE(lsmkv::consumeFixed64(footer_input, index_size));
    REQUIRE(lsmkv::consumeFixed64(footer_input, bloom_offset));
    REQUIRE(lsmkv::consumeFixed64(footer_input, bloom_size));
    REQUIRE(lsmkv::consumeFixed64(footer_input, magic));
    REQUIRE(footer_input.empty());
    REQUIRE(index_offset == 53);
    REQUIRE(index_size == 59);
    REQUIRE(bloom_offset == 0);
    REQUIRE(bloom_size == 0);
    REQUIRE(magic == lsmkv::kSSTableMagic);
    std::string expected_data;
    expected_data.append(apple_entry);
    expected_data.append(banana_entry);
    expected_data.append(cat_entry);
    REQUIRE(std::string_view(file_data.data(), static_cast<std::size_t>(index_offset)) == expected_data);
    std::string_view index_input(file_data.data() + index_offset, static_cast<std::size_t>(index_size));
    std::string_view last_internal_key;
    std::uint64_t block_offset = 0;
    std::uint64_t block_size = 0;
    REQUIRE(lsmkv::consumeLengthPrefixedSlice(index_input, last_internal_key));
    REQUIRE(last_internal_key == banana_key);
    REQUIRE(lsmkv::consumeFixed64(index_input, block_offset));
    REQUIRE(lsmkv::consumeFixed64(index_input, block_size));
    REQUIRE(block_offset == 0);
    REQUIRE(block_size == 40);
    REQUIRE(lsmkv::consumeLengthPrefixedSlice(index_input, last_internal_key));
    REQUIRE(last_internal_key == cat_key);
    REQUIRE(lsmkv::consumeFixed64(index_input, block_offset));
    REQUIRE(lsmkv::consumeFixed64(index_input, block_size));
    REQUIRE(block_offset == 40);
    REQUIRE(block_size == 13);
    REQUIRE(index_input.empty());
    REQUIRE(file_data.size() == index_offset + index_size + lsmkv::kSSTableFooterSize);
    std::filesystem::remove(path);
}

TEST_CASE("SSTableWriter rejects unordered and duplicate keys", "[sstable-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_order_test.sst";
    std::filesystem::remove(path);
    std::string apple_key;
    std::string banana_key;
    REQUIRE(lsmkv::appendInternalKey(apple_key, "apple", 10, lsmkv::ValueType::kPut));
    REQUIRE(lsmkv::appendInternalKey(banana_key, "banana", 20, lsmkv::ValueType::kPut));
    lsmkv::SSTableWriter writer(path.string());
    REQUIRE(writer.isOpen());
    REQUIRE(writer.add(banana_key, "yellow"));
    REQUIRE_FALSE(writer.add(apple_key, "red"));
    REQUIRE_FALSE(writer.add(banana_key, "yellow"));
    REQUIRE(writer.finish());
    std::filesystem::remove(path);
}

TEST_CASE("SSTableWriter preserves existing files", "[sstable-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_existing_file_test.sst";
    std::filesystem::remove(path);
    {
        std::ofstream file(path, std::ios::binary);
        file << "existing";
    }
    lsmkv::SSTableWriter writer(path.string());
    REQUIRE_FALSE(writer.isOpen());
    std::ifstream file(path, std::ios::binary);
    const std::string file_data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    REQUIRE(file_data == "existing");
    std::filesystem::remove(path);
}

TEST_CASE("SSTableWriter rejects zero block size", "[sstable-writer]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_zero_block_test.sst";
    std::filesystem::remove(path);
    lsmkv::SSTableWriter writer(path.string(), 0);
    REQUIRE_FALSE(writer.isOpen());
    REQUIRE_FALSE(std::filesystem::exists(path));
}
