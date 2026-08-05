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
    src/sstable_writer.cpp
    src/sstable_reader.cpp
    src/manifest.cpp
)
target_include_directories(lsmkv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()

add_executable(lsmkv_tests
    tests/dummy_test.cpp
    tests/coding_test.cpp
    tests/internal_key_test.cpp
    tests/memtable_test.cpp
    tests/wal_writer_test.cpp
    tests/wal_reader_test.cpp
    tests/db_test.cpp
    tests/sstable_writer_test.cpp
    tests/sstable_reader_test.cpp
    tests/manifest_test.cpp
)
add_executable(sstable_dump
    tools/sstable_dump.cpp
)
target_link_libraries(sstable_dump PRIVATE lsmkv)
target_link_libraries(lsmkv_tests PRIVATE catch2 lsmkv)

add_test(NAME lsmkv_tests COMMAND lsmkv_tests)
