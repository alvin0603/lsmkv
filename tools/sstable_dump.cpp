#include <lsmkv/coding.h>
#include <lsmkv/internal_key.h>
#include <lsmkv/sstable_format.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        std::cerr << "usage: sstable_dump <file.sst>\n";
        return 1;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if(!file.is_open())
    {
        std::cerr << "failed to open SSTable\n";
        return 1;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    if(file_size < static_cast<std::streamoff>(lsmkv::kSSTableFooterSize))
    {
        std::cerr << "file is smaller than footer\n";
        return 1;
    }
    const std::uint64_t footer_offset = static_cast<std::uint64_t>(file_size) - lsmkv::kSSTableFooterSize;
    file.seekg(static_cast<std::streamoff>(footer_offset), std::ios::beg);
    std::string footer(lsmkv::kSSTableFooterSize, '\0');
    file.read(footer.data(), static_cast<std::streamsize>(footer.size()));
    if(!file)
    {
        std::cerr << "failed to read footer\n";
        return 1;
    }
    std::string_view footer_input = footer;
    std::uint64_t index_offset = 0;
    std::uint64_t index_size = 0;
    std::uint64_t bloom_offset = 0;
    std::uint64_t bloom_size = 0;
    std::uint64_t magic = 0;
    if(!lsmkv::consumeFixed64(footer_input, index_offset) ||
       !lsmkv::consumeFixed64(footer_input, index_size) ||
       !lsmkv::consumeFixed64(footer_input, bloom_offset) ||
       !lsmkv::consumeFixed64(footer_input, bloom_size) ||
       !lsmkv::consumeFixed64(footer_input, magic))
    {
        std::cerr << "invalid footer\n";
        return 1;
    }
    if(magic != lsmkv::kSSTableMagic)
    {
        std::cerr << "invalid SSTable magic\n";
        return 1;
    }
    if(index_offset > footer_offset || index_size > footer_offset - index_offset)
    {
        std::cerr << "invalid index range\n";
        return 1;
    }
    if(index_size > std::numeric_limits<std::size_t>::max())
    {
        std::cerr << "index is too large\n";
        return 1;
    }
    std::cout << "file_size: " << file_size << '\n';
    std::cout << "index_offset: " << index_offset << '\n';
    std::cout << "index_size: " << index_size << '\n';
    std::cout << "bloom_offset: " << bloom_offset << '\n';
    std::cout << "bloom_size: " << bloom_size << '\n';
    std::cout << "magic: 0x" << std::hex << magic << std::dec << '\n';
    file.seekg(static_cast<std::streamoff>(index_offset), std::ios::beg);
    std::string index_block(static_cast<std::size_t>(index_size), '\0');
    file.read(index_block.data(), static_cast<std::streamsize>(index_block.size()));
    if(!file)
    {
        std::cerr << "failed to read index block\n";
        return 1;
    }
    std::string_view index_input = index_block;
    std::size_t block_number = 0;
    while(!index_input.empty())
    {
        std::string_view last_internal_key;
        std::uint64_t block_offset = 0;
        std::uint64_t block_size = 0;
        if(!lsmkv::consumeLengthPrefixedSlice(index_input,last_internal_key) ||
           !lsmkv::consumeFixed64(index_input, block_offset) ||
           !lsmkv::consumeFixed64(index_input, block_size))
        {
            std::cerr << "invalid index entry\n";
            return 1;
        }
        lsmkv::ParsedInternalKey parsed_key;
        if(!lsmkv::parseInternalKey(last_internal_key, parsed_key))
        {
            std::cerr << "invalid internal key in index\n";
            return 1;
        }
        const char* type = parsed_key.type == lsmkv::ValueType::kPut ? "put" : "delete";
        std::cout << "block " << block_number;
        std::cout << ": last_key=\"" << parsed_key.user_key;
        std::cout  << "\" sequence=" << parsed_key.sequence;
        std::cout << " type=" << type;
        std::cout << " offset=" << block_offset;
        std::cout << " size=" << block_size << '\n';
        block_number++;
    }
    return 0;
}
