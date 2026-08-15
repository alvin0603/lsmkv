#include <catch_amalgamated.hpp>
#include <lsmkv/internal_key.h>
#include <lsmkv/memtable.h>
#include "internal/merging_iterator.h"

#include <string>

TEST_CASE("MergingIterator keeps newest entries and tombstones", "[merging-iterator]")
{
    lsmkv::MemTable old_memtable;
    REQUIRE(old_memtable.add(10, lsmkv::ValueType::kPut, "apple", "old"));
    REQUIRE(old_memtable.add(20, lsmkv::ValueType::kPut, "banana", "yellow"));
    REQUIRE(old_memtable.add(30, lsmkv::ValueType::kPut, "cat", "black"));
    lsmkv::MemTable new_memtable;
    REQUIRE(new_memtable.add(40, lsmkv::ValueType::kPut, "apple", "new"));
    REQUIRE(new_memtable.add(50, lsmkv::ValueType::kDelete, "banana", ""));
    REQUIRE(new_memtable.add(60, lsmkv::ValueType::kPut, "dog", "brown"));
    lsmkv::MergingIterator iterator;
    iterator.addMemTable(old_memtable);
    iterator.addMemTable(new_memtable);
    iterator.initialize();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    lsmkv::ParsedInternalKey parsed_key;
    REQUIRE(lsmkv::parseInternalKey(iterator.internalKey(), parsed_key));
    REQUIRE(parsed_key.user_key == "apple");
    REQUIRE(parsed_key.sequence == 40);
    REQUIRE(parsed_key.type == lsmkv::ValueType::kPut);
    REQUIRE(iterator.value() == "new");
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(lsmkv::parseInternalKey(iterator.internalKey(), parsed_key));
    REQUIRE(parsed_key.user_key == "banana");
    REQUIRE(parsed_key.sequence == 50);
    REQUIRE(parsed_key.type == lsmkv::ValueType::kDelete);
    REQUIRE(iterator.value().empty());
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(lsmkv::parseInternalKey(iterator.internalKey(), parsed_key));
    REQUIRE(parsed_key.user_key == "cat");
    REQUIRE(parsed_key.sequence == 30);
    REQUIRE(parsed_key.type == lsmkv::ValueType::kPut);
    REQUIRE(iterator.value() == "black");
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE(iterator.valid());
    REQUIRE(lsmkv::parseInternalKey(iterator.internalKey(), parsed_key));
    REQUIRE(parsed_key.user_key == "dog");
    REQUIRE(parsed_key.sequence == 60);
    REQUIRE(parsed_key.type == lsmkv::ValueType::kPut);
    REQUIRE(iterator.value() == "brown");
    iterator.next();
    REQUIRE(iterator.ok());
    REQUIRE_FALSE(iterator.valid());
}
