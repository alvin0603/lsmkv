#pragma once

#include <cstddef>
#include <cstdint>

namespace lsmkv 
{
    inline constexpr std::size_t kDefaultDataBlockSize = 4096;
    inline constexpr std::size_t kSSTableFooterSize = 40;
    inline constexpr std::uint64_t kSSTableMagic = 0x6C736D6B765F5353;
}