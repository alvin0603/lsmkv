#pragma once

#include <lsmkv/sstable_reader.h>

#include <string>
#include <string_view>
#include <vector>

namespace lsmkv
{
    struct CompactionOutput
    {
        std::string smallest_key;
        std::string largest_key;
    };
    bool writeCompactedTable(std::string_view output_path, const std::vector<const SSTableReader*>& readers, CompactionOutput& output);
}