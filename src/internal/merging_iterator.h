#pragma once

#include <lsmkv/memtable.h>
#include <lsmkv/sstable_reader.h>

#include <memory>
#include <string_view>

namespace lsmkv
{
    class MergingIterator
    {
        public:
            MergingIterator();
            ~MergingIterator();
            MergingIterator(const MergingIterator&) = delete;
            MergingIterator& operator=(const MergingIterator&) = delete;
            void addMemTable(const MemTable& memtable);
            void addSSTable(const SSTableReader& reader);
            void initialize();
            bool valid() const;
            bool ok() const;
            std::string_view internalKey() const;
            std::string_view value() const;
            void next();
        private:
            class Impl;
            std::unique_ptr<Impl> impl_;
    };
}
