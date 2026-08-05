#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsmkv
{
    inline constexpr std::uint64_t kManifestMagic = 0x6C736D6B765F4D46;

    struct TableMetaData
    {
        std::uint64_t file_id;
        std::uint32_t level;
        std::uint64_t file_size;
        std::string smallest_key;
        std::string largest_key;
    };
    struct ManifestState
    {
        std::uint64_t next_file_id = 1;
        std::uint64_t last_sequence = 0;
        std::uint64_t durable_wal_epoch = 0;
        std::vector<TableMetaData> tables;
    };
    bool encodeManifest(const ManifestState& state, std::string& output);
    bool decodeManifest(std::string_view input, ManifestState& state);
    bool loadManifest(std::string_view directory, ManifestState& state);
    bool saveManifest(std::string_view directory, const ManifestState& state);
}