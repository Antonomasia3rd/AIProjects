#include "../dependencies/png_crc32.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>
#include <stdexcept>

static std::uint32_t Reference(std::uint32_t crc, const unsigned char* data, std::size_t length)
{
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}
int main()
{
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + seconds(20);
    int checks = 0;
    const auto require = [&](bool ok) { ++checks; if (!ok) throw std::runtime_error("CRC mismatch"); };
    try {
        const unsigned char known[] = "123456789";
        require(~aip::png::Crc32Update(0xFFFFFFFFu, known, 9) == 0xCBF43926u);
        const unsigned char iend[] = {'I','E','N','D'};
        require(~aip::png::Crc32Update(0xFFFFFFFFu, iend, 4) == 0xAE426082u);
        require(aip::png::Crc32Update(0x12345678u, nullptr, 0) == 0x12345678u);
        std::mt19937 random(0xC0FFEEu);
        for (int sample = 0; sample < 1000; ++sample) {
            std::vector<unsigned char> data(random() % 4097);
            for (auto& byte : data) byte = static_cast<unsigned char>(random());
            const auto seed = random();
            const auto expected = Reference(seed, data.data(), data.size());
            require(aip::png::Crc32Update(seed, data.data(), data.size()) == expected);
            auto incremental = seed;
            for (std::size_t offset = 0; offset < data.size();) {
                const auto length = (std::min)(data.size() - offset, static_cast<std::size_t>(random() % 63 + 1));
                incremental = aip::png::Crc32Update(incremental, data.data() + offset, length);
                offset += length;
            }
            require(incremental == expected);
            if (steady_clock::now() > deadline) throw std::runtime_error("CRC fixture deadline");
        }
        std::vector<unsigned char> data(16 * 1024 * 1024);
        for (auto& byte : data) byte = static_cast<unsigned char>(random());
        auto begin = steady_clock::now();
        const auto reference = Reference(0xFFFFFFFFu, data.data(), data.size());
        const auto referenceUs = duration_cast<microseconds>(steady_clock::now() - begin).count();
        begin = steady_clock::now();
        const auto optimized = aip::png::Crc32Update(0xFFFFFFFFu, data.data(), data.size());
        const auto optimizedUs = duration_cast<microseconds>(steady_clock::now() - begin).count();
        require(reference == optimized);
        std::printf("PNG CRC: %d checks passed; same 16 MiB, reference %lld us, table %lld us\n", checks,
            static_cast<long long>(referenceUs), static_cast<long long>(optimizedUs));
        return 0;
    } catch (const std::exception& ex) { std::printf("FAIL: %s\n", ex.what()); return 1; }
}
