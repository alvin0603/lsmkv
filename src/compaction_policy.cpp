#include "internal/compaction_policy.h"
#include <lsmkv/internal_key.h>

#include <utility>
#include <string_view>
#include <limits>
#include <map>

namespace
{
    struct UserKeyRange
    {
        std::string_view smallest_key;
        std::string_view largest_key;
    };
    bool parseUserKeyRange(const lsmkv::TableMetaData& table, UserKeyRange& range)
    {
        lsmkv::ParsedInternalKey smallest_key;
        lsmkv::ParsedInternalKey largest_key;
        if(!lsmkv::parseInternalKey(table.smallest_key, smallest_key) || !lsmkv::parseInternalKey(table.largest_key, largest_key))
            return false;
        range.smallest_key = smallest_key.user_key;
        range.largest_key = largest_key.user_key;
        return true;
    }
    bool rangesOverlap(const UserKeyRange& left, const UserKeyRange& right)
    {
        return left.smallest_key <= right.largest_key && right.smallest_key <= left.largest_key;
    }
    std::uint64_t levelCapacity(std::uint64_t level_base_size, std::uint32_t level)
    {
        std::uint64_t capacity = level_base_size;
        for(std::uint32_t current_level = 1; current_level < level; current_level++)
        {
            if(capacity > std::numeric_limits<std::uint64_t>::max() / 10)
                return std::numeric_limits<std::uint64_t>::max();
            capacity *= 10;
        }
        return capacity;
    }
}

namespace lsmkv
{
    bool TieredCompactionPolicy::createPlan(const std::vector<TableMetaData>& tables, std::size_t l0_compaction_trigger, CompactionPlan& plan) const
    {
        CompactionPlan tmp_plan;
        for(const TableMetaData& table : tables)
        {
            if(table.level == 0)
                tmp_plan.input_file_ids.push_back(table.file_id);
        }
        if(tmp_plan.input_file_ids.size() < l0_compaction_trigger)
            return false;
        tmp_plan.output_level = 1;
        plan = std::move(tmp_plan);
        return true;
    }
    LeveledCompactionPolicy::LeveledCompactionPolicy(std::uint64_t level_base_size): level_base_size_(level_base_size){}
    bool LeveledCompactionPolicy::createPlan(const std::vector<TableMetaData>& tables, std::size_t l0_compaction_trigger, CompactionPlan& plan) const
    {
        /* Level 0 -> Level 1 */
        if(l0_compaction_trigger == 0 || level_base_size_ == 0)
            return false;
        std::vector<const TableMetaData*> l0_tables;
        for(const TableMetaData& table: tables)
        {
            if(table.level == 0)
                l0_tables.push_back(&table);
        }
        if(l0_tables.size() >= l0_compaction_trigger)
        {
            UserKeyRange l0_range;
            if(!parseUserKeyRange(*l0_tables.front(), l0_range))
                return false;
            CompactionPlan tmp_plan;
            for(const TableMetaData* table: l0_tables)
            {
                UserKeyRange table_range;
                if(!parseUserKeyRange(*table, table_range))
                    return false;
                if(table_range.smallest_key < l0_range.smallest_key)
                    l0_range.smallest_key = table_range.smallest_key;
                if(table_range.largest_key > l0_range.largest_key)
                    l0_range.largest_key = table_range.largest_key;
                tmp_plan.input_file_ids.push_back(table->file_id);
            }
            for(const TableMetaData& table: tables)
            {
                if(table.level != 1)
                    continue;
                UserKeyRange table_range;
                if(!parseUserKeyRange(table, table_range))
                    return false;
                if(rangesOverlap(l0_range, table_range))
                    tmp_plan.input_file_ids.push_back(table.file_id);
            }
            tmp_plan.output_level = 1;
            plan = std::move(tmp_plan);
            return true;
        }

        /* Level n -> Level n+1 */
        std::map<std::uint32_t, std::uint64_t> level_sizes;
        for(const TableMetaData& table: tables)
        {
            if(table.level == 0)
                continue;
            std::uint64_t& current_size = level_sizes[table.level];
            if(current_size > std::numeric_limits<std::uint64_t>::max() - table.file_size)
                current_size = std::numeric_limits<std::uint64_t>::max();
            else
                current_size += table.file_size;
        }
        std::uint32_t source_level = 0;
        for(const auto& [level, size]: level_sizes)
        {
            if(size > levelCapacity(level_base_size_, level))
            {
                source_level = level;
                break;
            }
        }
        if(source_level == 0 || source_level == std::numeric_limits<std::uint32_t>::max())
            return false;
        const TableMetaData* source_table = nullptr;
        for(const TableMetaData& table: tables)
        {
            if(table.level != source_level)
                continue;
            if(source_table == nullptr || table.file_id < source_table->file_id)
                source_table = &table;
        }
        if(source_table == nullptr)
            return false;
        UserKeyRange source_range;
        if(!parseUserKeyRange(*source_table, source_range))
            return false;
        CompactionPlan tmp_plan;
        tmp_plan.input_file_ids.push_back(source_table->file_id);
        const std::uint32_t output_level = source_level + 1;
        for(const TableMetaData& table: tables)
        {
            if(table.level != output_level)
                continue;
            UserKeyRange table_range;
            if(!parseUserKeyRange(table, table_range))
                return false;
            if(rangesOverlap(source_range, table_range))
                tmp_plan.input_file_ids.push_back(table.file_id);
        }
        tmp_plan.output_level = output_level;
        plan = std::move(tmp_plan);
        return true;
    }
}
