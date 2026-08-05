#include "internal/file_names.h"

#include <charconv>
#include <string>
#include <string_view>

namespace
{
    bool parseFileNumber(const std::filesystem::path& path, std::string_view extension, std::uint64_t& number)
    {
        if(path.extension() != extension)
            return false;
        const std::string stem = path.stem().string();
        if(stem.empty())
            return false;
        const auto result = std::from_chars(stem.data(), stem.data() + stem.size(), number);
        if(result.ec != std::errc{} || result.ptr != stem.data() + stem.size())
            return false;
        if(number == 0)
            return false;
        return true;
    }
}

namespace lsmkv
{
    std::filesystem::path lockFilePath(const std::filesystem::path& directory)
    {
        return directory / "LOCK";
    }
    std::filesystem::path manifestFilePath(const std::filesystem::path& directory)
    {
        return directory / "MANIFEST";
    }
    std::filesystem::path temporaryManifestFilePath(const std::filesystem::path& directory)
    {
        return directory / "MANIFEST.tmp";
    }
    std::filesystem::path walFilePath(const std::filesystem::path& directory, std::uint64_t epoch)
    {
        return directory / (std::to_string(epoch) + ".wal");
    }
    std::filesystem::path sstableFilePath(const std::filesystem::path& directory, std::uint64_t file_id)
    {
        return directory / (std::to_string(file_id) + ".sst");
    }
    bool parseWalEpoch(const std::filesystem::path& path, std::uint64_t& epoch)
    {
        return parseFileNumber(path, ".wal", epoch);
    }
    bool parseSSTableFileId(const std::filesystem::path& path, std::uint64_t& file_id)
    {
        return parseFileNumber(path, ".sst", file_id);
    }
}
