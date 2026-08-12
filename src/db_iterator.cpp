#include <lsmkv/db_iterator.h>
#include <lsmkv/db.h>
#include <lsmkv/memtable.h>
#include <lsmkv/sstable_reader.h>
#include <lsmkv/internal_key.h>

#include <memory>
#include <string_view>
#include <vector>
#include <cstddef>
#include <queue>
#include <string>
#include <utility>

namespace lsmkv
{
    namespace
    {
        class IteratorSource // only used here
        {
            public:
                virtual ~IteratorSource() = default;
                virtual bool valid() const = 0;
                virtual bool ok() const = 0;
                virtual std::string_view internalKey() const = 0;
                virtual std::string_view value() const = 0;
                virtual void next() = 0;
        };

        class MemTableSource : public IteratorSource
        {
            public:
                explicit MemTableSource(const MemTable& memtable)
                {
                    iterator_ = memtable.begin();
                    end_ = memtable.end();
                }
                bool valid() const override
                {
                    return iterator_ != end_;
                }
                bool ok() const override
                {
                    return true;
                }
                std::string_view internalKey() const override
                {
                    if(!valid())
                        return {};
                    return iterator_->first;
                }
                std::string_view value() const override
                {
                    if(!valid())
                        return {};
                    return iterator_->second;
                }
                void next() override
                {
                    if(valid())
                        iterator_++;
                }
            private:
                MemTable::const_iterator iterator_;
                MemTable::const_iterator end_;
        };

        class SSTableSource : public IteratorSource
        {
            public:
                explicit SSTableSource(const SSTableReader& reader): iterator_(reader.newIterator()){}
                bool valid() const override
                {
                    return iterator_.valid();
                }
                bool ok() const override
                {
                    return iterator_.ok();
                }
                std::string_view internalKey() const override
                {
                    return iterator_.internalKey();
                }
                std::string_view value() const override
                {
                    return iterator_.value();
                }
                void next() override
                {
                    iterator_.next();
                }
            private:
                SSTableIterator iterator_;
        };
    } // end of anonymous namespace

    class DBIterator::Impl
    {
        public:
            void addMemTable(const MemTable& memtable)
            {
                sources_.push_back(std::make_unique<MemTableSource>(memtable));
            }
            void addSSTable(const SSTableReader& reader)
            {
                sources_.push_back(std::make_unique<SSTableSource>(reader));
            }
            void initialize()
            {
                for(std::size_t i = 0; i < sources_.size(); i++)
                {
                    if(!pushSource(i))
                        return;
                }
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
                findNext();
            }
        private:
            struct HeapEntry
            {
                std::size_t source_index;
                std::string internal_key;
            };
            struct HeapComparator
            {
                bool operator()(const HeapEntry& left, const HeapEntry& right) const
                {
                    InternalKeyComparator comparator;
                    return comparator(right.internal_key, left.internal_key);
                }
            };
            bool pushSource(std::size_t source_index)
            {
                IteratorSource& source = *sources_[source_index];
                if(!source.ok())
                {
                    setError();
                    return false;
                }
                if(source.valid())
                    heap_.push({source_index, std::string(source.internalKey())});
                return true;
            }
            bool advanceSource(std::size_t source_index)
            {
                sources_[source_index]->next();
                return pushSource(source_index);
            }
            bool findNext()
            {
                valid_ = false;
                current_key_.clear();
                current_value_.clear();
                while(!heap_.empty())
                {
                    // trace the source to get the value
                    HeapEntry newest_entry = heap_.top();
                    heap_.pop();
                    IteratorSource& newest_source = *sources_[newest_entry.source_index];
                    ParsedInternalKey newest_key;
                    if(!parseInternalKey(newest_entry.internal_key, newest_key))
                    {
                        setError();
                        return false;
                    }
                    std::string user_key(newest_key.user_key);
                    const ValueType newest_type = newest_key.type;
                    std::string newest_value(newest_source.value());
                    if(!advanceSource(newest_entry.source_index))
                        return false;
                    
                    // discard the outdated (including the deleted)
                    while(!heap_.empty())
                    {
                        ParsedInternalKey next_key;
                        if(!parseInternalKey(heap_.top().internal_key, next_key))
                        {
                            setError();
                            return false;
                        }
                        if(next_key.user_key != user_key)
                            break;
                        const std::size_t source_index = heap_.top().source_index;
                        heap_.pop();
                        if(!advanceSource(source_index))
                            return false;
                    }
                    if(newest_type == ValueType::kDelete)
                        continue;
                    current_key_ = std::move(user_key);
                    current_value_ = std::move(newest_value);
                    valid_ = true;
                    return true;
                }
                
                return true;
            }
            void setError()
            {
                // In case error occur during scan
                ok_ = false;
                valid_ = false;
                current_key_.clear();
                current_value_.clear();
                while(!heap_.empty())
                    heap_.pop();
            }
            std::vector<std::unique_ptr<IteratorSource>> sources_;
            std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapComparator> heap_;
            std::string current_key_;
            std::string current_value_;
            bool valid_ = false;
            bool ok_ = true;
    }; // end of merge implementation

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