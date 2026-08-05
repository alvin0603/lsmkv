#pragma once

#include <cstddef>
#include <string_view>

namespace lsmkv
{
    enum class FileReadResult
    {
        kComplete,
        kEnd,
        kPartial,
        kError
    };

    bool writeAll(int fd, std::string_view data);
    FileReadResult readExact(int fd, char* output, std::size_t size);
    bool syncFile(int fd);
}
