#pragma once

#include <memory>
#include <string_view>

namespace lsmkv
{
    class DB;
    class DBIterator
    {
        public:
            ~DBIterator();
            DBIterator(const DBIterator&) = delete;
            DBIterator& operator=(const DBIterator&) = delete;
            bool valid() const;
            bool ok() const;
            std::string_view key() const;
            std::string_view value() const;
            void next();
        private:
            explicit DBIterator(const DB* db);
            class Impl;
            std::unique_ptr<Impl> impl_;
            friend class DB;
    };
}