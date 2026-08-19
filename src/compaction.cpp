#include "internal/compaction.h"
#include "internal/merging_iterator.h"
#include <lsmkv/sstable_writer.h>

#include <string>
#include <utility>

namespace lsmkv
{
    bool writeCompactedTable(std::string_view output_path, const std::vector<const SSTableReader*>& readers, CompactionOutput& output, std::size_t bloom_bits_per_key, std::uint32_t bloom_num_hashes)
    {
        if(readers.empty())
            return false;
        MergingIterator iterator;
        for(const SSTableReader* reader : readers)
        {
            if(reader == nullptr || !reader->isOpen())
                return false;
            iterator.addSSTable(*reader);
        }
        iterator.initialize();
        if(!iterator.ok())
            return false;
        SSTableWriter writer(output_path, kDefaultDataBlockSize, bloom_bits_per_key, bloom_num_hashes);
        if(!writer.isOpen())
            return false;
        CompactionOutput compacted;
        bool has_entry = false;
        // merge + write new SSTable
        while(iterator.valid())
        {
            if(!has_entry)
            {
                compacted.smallest_key.assign(iterator.internalKey());
                has_entry = true;
            }
            compacted.largest_key.assign(iterator.internalKey());

            if(!writer.add(iterator.internalKey(), iterator.value()))
                return false;
            iterator.next();
        }
        if(!iterator.ok())
            return false;
        if(!has_entry)
            return false;
        if(!writer.finish())
            return false;
        output = std::move(compacted);
        return true;
    }
}
