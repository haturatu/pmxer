#include "../../src/render/ImageDecoder.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void put32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
        bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
}

std::vector<std::uint8_t> dds(std::uint32_t format, std::size_t blockSize) {
    std::vector<std::uint8_t> bytes(128U + blockSize);
    put32(bytes, 0, 0x20534444U);
    put32(bytes, 4, 124);
    put32(bytes, 12, 4);
    put32(bytes, 16, 4);
    put32(bytes, 76, 32);
    put32(bytes, 80, 4);
    put32(bytes, 84, format);
    return bytes;
}

} // namespace

int main() {
    auto bc1 = dds(0x31545844U, 8);
    bc1[128] = 0x00;
    bc1[129] = 0xf8;
    const auto red = pmxer::decodeImageBytes(bc1);
    assert(red);
    assert(red.width == 4 && red.height == 4);
    assert(red.rgba[0] == 255 && red.rgba[1] == 0 && red.rgba[2] == 0 && red.rgba[3] == 255);

    auto bc2 = dds(0x33545844U, 16);
    for (std::size_t i = 0; i < 8; ++i)
        bc2[128 + i] = 0xff;
    bc2[136] = 0xe0;
    bc2[137] = 0x07;
    const auto green = pmxer::decodeImageBytes(bc2);
    assert(green && green.rgba[1] == 255 && green.rgba[3] == 255);

    auto bc3 = dds(0x35545844U, 16);
    bc3[128] = 255;
    bc3[129] = 0;
    bc3[136] = 0x1f;
    bc3[137] = 0x00;
    const auto blue = pmxer::decodeImageBytes(bc3);
    assert(blue && blue.rgba[2] == 255 && blue.rgba[3] == 255);

    bc3.resize(130);
    const auto truncated = pmxer::decodeImageBytes(bc3);
    assert(!truncated);
    assert(!truncated.error.empty());
    return 0;
}
