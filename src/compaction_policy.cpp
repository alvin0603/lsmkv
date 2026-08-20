#include "internal/compaction_policy.h"

#include <utility>

namespace lsmkv
{
    bool TieredCompactionPolicy::createPlan(const std::vector<TableMetaData>& tables, std::size_t l0_compaction_trigger, CompactionPlan& plan) const
    {
        // find the L0
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
}
