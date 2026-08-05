#pragma once

#include <lsmkv/internal_key.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace lsmkv
{
    enum class LookupResult
    {
        kNotFound,
        kFound,
        kDeleted
    };
    class MemTable
    {
        public:
            bool add(std::uint64_t sequence, ValueType type, std::string_view user_key, std::string_view value);
            LookupResult get(std::string_view user_key, std::string& value) const;
            std::size_t approximateMemoryUsage() const;
            using const_iterator = std::map<std::string, std::string, InternalKeyComparator>::const_iterator;
            const_iterator begin() const;
            const_iterator end() const;
            bool empty() const;
        private:
            std::map<std::string, std::string, InternalKeyComparator> table_;
            std::size_t approximate_memory_usage_ = 0;
    };
}
