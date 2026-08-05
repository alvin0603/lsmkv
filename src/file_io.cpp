#include "internal/file_io.h"

#include <cerrno>
#include <unistd.h>

namespace lsmkv
{
    bool writeAll(int fd, std::string_view data)
    {
        if(fd == -1)
            return false;
        std::size_t total_written = 0;
        while(total_written < data.size())
        {
            const ssize_t result = ::write(fd, data.data() + total_written, data.size() - total_written);
            if(result == -1)
            {
                if(errno == EINTR)
                    continue;
                return false;
            }
            if(result == 0)
                return false;
            total_written += static_cast<std::size_t>(result);
        }
        return true;
    }
    FileReadResult readExact(int fd, char* output, std::size_t size)
    {
        if(fd == -1)
            return FileReadResult::kError;
        std::size_t total_read = 0;
        while(total_read < size)
        {
            const ssize_t result = ::read(fd, output + total_read, size - total_read);
            if(result == -1)
            {
                if(errno == EINTR)
                    continue;
                return FileReadResult::kError;
            }
            if(result == 0)
            {
                if(total_read == 0)
                    return FileReadResult::kEnd;
                return FileReadResult::kPartial;
            }
            total_read += static_cast<std::size_t>(result);
        }
        return FileReadResult::kComplete;
    }
    bool syncFile(int fd)
    {
        if(fd == -1)
            return false;
        while(::fsync(fd) == -1)
        {
            if(errno == EINTR)
                continue;
            return false;
        }
        return true;
    }
}
