#pragma once
#include "pattern.h"

namespace aip { namespace asus {
constexpr std::uint32_t ControlCode = 0x0022240C;
constexpr std::uint32_t SetMethod = 0x53564544;
constexpr std::uint32_t MicLedId = 0x00040017;
constexpr std::uint32_t KeyboardId = 0x00050021;
inline std::array<unsigned char, 16> SetRequest(Device device, int state) {
    std::array<unsigned char, 16> bytes{};
    const std::uint32_t words[] = {SetMethod, 8, device == Device::Mic ? MicLedId : KeyboardId, static_cast<std::uint32_t>(state)};
    for (std::size_t word = 0; word < 4; ++word) for (unsigned byte = 0; byte < 4; ++byte)
        bytes[word * 4 + byte] = static_cast<unsigned char>(words[word] >> (8 * byte));
    return bytes;
}
inline bool AcceptedResponse(const unsigned char* bytes, std::size_t length) {
    return bytes && length >= 4 && length <= 16 && bytes[0] == 1 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
}
} }
