cmake_minimum_required(VERSION 3.16)

project(lsmkv VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(catch2 STATIC
    third_party/catch_amalgamated.cpp
)
target_include_directories(catch2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
add_library(lsmkv STATIC
    src/coding.cpp
    src/file_io.cpp
    src/file_names.cpp
    src/internal_key.cpp
    src/memtable.cpp
    src/wal_writer.cpp
    src/wal_reader.cpp
    src/db.cpp
    src/db_iterator.cpp
    src/merging_iterator.cpp
    src/sstable_writer.cpp
    src/sstable_reader.cpp
    src/manifest.cpp
    src/compaction.cpp
    src/bloom_filter.cpp
    src/block_cache.cpp
)
target_include_directories(lsmkv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(lsmkv_bench_support STATIC
    bench/workload.cpp
    bench/metrics.cpp
)
target_include_directories(lsmkv_bench_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)

enable_testing()

add_executable(lsmkv_tests
    tests/dummy_test.cpp
    tests/block_cache_test.cpp
    tests/bloom_filter_test.cpp
    tests/coding_test.cpp
    tests/compaction_test.cpp
    tests/internal_key_test.cpp
    tests/memtable_test.cpp
    tests/wal_writer_test.cpp
    tests/wal_reader_test.cpp
    tests/db_test.cpp
    tests/merging_iterator_test.cpp
    tests/sstable_writer_test.cpp
    tests/sstable_reader_test.cpp
    tests/manifest_test.cpp
    tests/workload_test.cpp
    tests/metrics_test.cpp
)
add_executable(sstable_dump
    tools/sstable_dump.cpp
)
add_executable(lsmkv_bench
    bench/bench_main.cpp
)
add_executable(lsmkv_crash_fuzz
    tools/crash_fuzz.cpp
)
target_link_libraries(sstable_dump PRIVATE lsmkv)
target_link_libraries(lsmkv_bench PRIVATE lsmkv lsmkv_bench_support)
target_link_libraries(lsmkv_crash_fuzz PRIVATE lsmkv)
target_link_libraries(lsmkv_tests PRIVATE catch2 lsmkv lsmkv_bench_support)
target_include_directories(lsmkv_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(lsmkv_crash_fuzz PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME lsmkv_tests COMMAND lsmkv_tests)
add_test(NAME lsmkv_crash_fuzz_smoke COMMAND lsmkv_crash_fuzz --iterations 3 --seed 1)
set_tests_properties(lsmkv_crash_fuzz_smoke PROPERTIES TIMEOUT 60)
