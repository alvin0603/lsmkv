#include <lsmkv/db_iterator.h>
#include <lsmkv/db.h>
#include <lsmkv/internal_key.h>
#include "internal/merging_iterator.h"

#include <memory>
#include <string>
#include <string_view>

namespace lsmkv
{
    class DBIterator::Impl
    {
        public:
            void addMemTable(const MemTable& memtable)
            {
                merging_iterator_.addMemTable(memtable);
            }
            void addSSTable(const SSTableReader& reader)
            {
                merging_iterator_.addSSTable(reader);
            }
            void initialize()
            {
                merging_iterator_.initialize();
                findNext();
            }
            bool valid() const
            {
                return valid_;
            }
            bool ok() const
            {
                return ok_;
            }
            std::string_view key() const
            {
                if(!valid_)
                    return {};
                return current_key_;
            }
            std::string_view value() const
            {
                if(!valid_)
                    return {};
                return current_value_;
            }
            void next()
            {
                if(!valid_)
                    return;
                merging_iterator_.next();
                findNext();
            }
        private:
            bool findNext()
            {
                valid_ = false;
                current_key_.clear();
                current_value_.clear();
                while(merging_iterator_.valid())
                {
                    ParsedInternalKey parsed_key;
                    if(!parseInternalKey(merging_iterator_.internalKey(), parsed_key))
                    {
                        ok_ = false;
                        return false;
                    }
                    if(parsed_key.type == ValueType::kDelete)
                    {
                        merging_iterator_.next();
                        continue;
                    }
                    current_key_.assign(parsed_key.user_key);
                    current_value_.assign(merging_iterator_.value());
                    valid_ = true;
                    return true;
                }
                if(!merging_iterator_.ok())
                {
                    ok_ = false;
                    return false;
                }
                return true;
            }
            MergingIterator merging_iterator_;
            std::string current_key_;
            std::string current_value_;
            bool valid_ = false;
            bool ok_ = true;
    };

    DBIterator::DBIterator(const DB* db)
    {
        impl_ = std::make_unique<Impl>();
        impl_->addMemTable(db->memtable_);
        if(db->immutable_memtable_)
            impl_->addMemTable(*db->immutable_memtable_);
        for(const auto& table : db->opened_tables_)
            impl_->addSSTable(*table.reader);
        impl_->initialize();
    }
    DBIterator::~DBIterator() = default;
    bool DBIterator::valid() const
    {
        return impl_->valid();
    }
    bool DBIterator::ok() const
    {
        return impl_->ok();
    }
    std::string_view DBIterator::key() const
    {
        return impl_->key();
    }
    std::string_view DBIterator::value() const
    {
        return impl_->value();
    }
    void DBIterator::next()
    {
        impl_->next();
    }
}
