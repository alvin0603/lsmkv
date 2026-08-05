#pragma once

#include <cstdint>
#include <filesystem>

namespace lsmkv
{
    std::filesystem::path lockFilePath(const std::filesystem::path& directory);
    std::filesystem::path manifestFilePath(const std::filesystem::path& directory);
    std::filesystem::path temporaryManifestFilePath(const std::filesystem::path& directory);
    std::filesystem::path walFilePath(const std::filesystem::path& directory, std::uint64_t epoch);
    std::filesystem::path sstableFilePath(const std::filesystem::path& directory, std::uint64_t file_id);
    bool parseWalEpoch(const std::filesystem::path& path, std::uint64_t& epoch);
    bool parseSSTableFileId(const std::filesystem::path& path, std::uint64_t& file_id);
}
