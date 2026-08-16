#include <catch_amalgamated.hpp>
#include <lsmkv/bloom_filter.h>
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
    REQUIRE(bloom_offset == index_offset + index_size);
    REQUIRE(bloom_size == 20);
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
    std::string_view bloom_block(file_data.data() + bloom_offset, static_cast<std::size_t>(bloom_size));
    lsmkv::BloomFilter bloom_filter;
    REQUIRE(bloom_filter.decode(bloom_block));
    REQUIRE(bloom_filter.mayContain("apple"));
    REQUIRE(bloom_filter.mayContain("banana"));
    REQUIRE(bloom_filter.mayContain("cat"));
    REQUIRE(file_data.size() == bloom_offset + bloom_size + lsmkv::kSSTableFooterSize);
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

TEST_CASE("SSTableWriter rejects invalid Bloom filter settings", "[sstable-writer]")
{
    const std::filesystem::path zero_bits_path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_zero_bloom_bits_test.sst";
    const std::filesystem::path zero_hashes_path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_zero_bloom_hashes_test.sst";
    const std::filesystem::path excessive_hashes_path = std::filesystem::temp_directory_path() / "lsmkv_sstable_writer_excessive_bloom_hashes_test.sst";
    std::filesystem::remove(zero_bits_path);
    std::filesystem::remove(zero_hashes_path);
    std::filesystem::remove(excessive_hashes_path);
    lsmkv::SSTableWriter zero_bits_writer(zero_bits_path.string(), lsmkv::kDefaultDataBlockSize, 0, lsmkv::kDefaultBloomHashCount);
    REQUIRE_FALSE(zero_bits_writer.isOpen());
    lsmkv::SSTableWriter zero_hashes_writer(zero_hashes_path.string(), lsmkv::kDefaultDataBlockSize, lsmkv::kDefaultBloomBitsPerKey, 0);
    REQUIRE_FALSE(zero_hashes_writer.isOpen());
    lsmkv::SSTableWriter excessive_hashes_writer(excessive_hashes_path.string(), lsmkv::kDefaultDataBlockSize, lsmkv::kDefaultBloomBitsPerKey, lsmkv::kMaxBloomHashCount + 1);
    REQUIRE_FALSE(excessive_hashes_writer.isOpen());
    REQUIRE_FALSE(std::filesystem::exists(zero_bits_path));
    REQUIRE_FALSE(std::filesystem::exists(zero_hashes_path));
    REQUIRE_FALSE(std::filesystem::exists(excessive_hashes_path));
}
