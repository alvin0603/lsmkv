#include <lsmkv/memtable.h>

#include <utility>

namespace lsmkv
{
    bool MemTable::add(std::uint64_t sequence, ValueType type, std::string_view user_key, std::string_view value)
    {
        std::string internal_key;
        if(!appendInternalKey(internal_key, user_key, sequence, type))
            return false;
        const std::size_t table_size = internal_key.size() + value.size();
        const auto insert_result = table_.emplace(std::move(internal_key), std::string(value)); // string_view -> string cuz map needs to own the value
        if (!insert_result.second)
            return false;
        approximate_memory_usage_ += table_size;
        return true;
    }
    LookupResult MemTable::get(std::string_view user_key, std::string& value) const
    {
        std::string lookup_key;
        if(!appendInternalKey(lookup_key, user_key, kMaxSequenceNumber, ValueType::kPut))
            return LookupResult::kNotFound;
        const auto it = table_.lower_bound(lookup_key);
        if(it == table_.end())
            return LookupResult::kNotFound;
        ParsedInternalKey parsed_key;
        if(!parseInternalKey(it->first, parsed_key))
            return LookupResult::kNotFound;
        if(parsed_key.user_key != user_key)
            return LookupResult::kNotFound;
        if(parsed_key.type == ValueType::kDelete)
            return LookupResult::kDeleted;
        value = it->second;
        return LookupResult::kFound;
    }
    std::size_t MemTable::approximateMemoryUsage() const
    {
        return approximate_memory_usage_;
    }
    MemTable::const_iterator MemTable::begin() const
    {
        return table_.begin();
    }
    MemTable::const_iterator MemTable::end() const
    {
        return table_.end();
    }
    bool MemTable::empty() const
    {
        return table_.empty();
    }
}