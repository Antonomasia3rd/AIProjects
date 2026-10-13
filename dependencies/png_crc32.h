#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace aip { namespace png {
// PNG uses the reflected CRC-32 polynomial. Keep the incremental state
// uncomplemented so callers can stream a chunk's type and payload separately.
constexpr std::array<std::uint32_t, 256> MakeCrc32Table() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::size_t value = 0; value < table.size(); ++value) {
        auto crc = static_cast<std::uint32_t>(value);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        table[value] = crc;
    }
    return table;
}

inline std::uint32_t Crc32Update(std::uint32_t crc, const unsigned char* data, std::size_t length) noexcept
{
    static constexpr auto table = MakeCrc32Table();
    const auto* lookup = table.data();
    for (std::size_t i = 0; i < length; ++i)
        crc = lookup[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}
} }
