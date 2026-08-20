#pragma once

#include <lsmkv/manifest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lsmkv
{
    struct CompactionPlan
    {
        std::vector<std::uint64_t> input_file_ids;
        std::uint32_t output_level = 0;
    };
    class CompactionPolicy
    {
        public:
            virtual ~CompactionPolicy() = default;
            virtual bool createPlan(const std::vector<TableMetaData>& tables, std::size_t l0_compaction_trigger, CompactionPlan& plan) const = 0;
    };
    class TieredCompactionPolicy final : public CompactionPolicy
    {
        public:
            bool createPlan(const std::vector<TableMetaData>& tables, std::size_t l0_compaction_trigger, CompactionPlan& plan) const override;
    };
}
