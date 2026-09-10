#include "../../src/render/ImageDecoder.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void put32(std::vector<std::uint8_t> &bytes, std::size_t offset,
           std::uint32_t value) {
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
  assert(red.rgba[0] == 255 && red.rgba[1] == 0 && red.rgba[2] == 0 &&
         red.rgba[3] == 255);

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

  auto largeTruncated = dds(0x31545844U, 0);
  put32(largeTruncated, 12, 16384);
  put32(largeTruncated, 16, 16384);
  const auto rejectedBeforeDecode = pmxer::decodeImageBytes(largeTruncated);
  assert(!rejectedBeforeDecode &&
         rejectedBeforeDecode.error == "invalid DDS dimensions");
  assert(rejectedBeforeDecode.rgba.empty());

  auto rgba = dds(0, 4);
  put32(rgba, 80, 0x41U);
  put32(rgba, 88, 32);
  put32(rgba, 92, 0x00ff0000U);
  put32(rgba, 96, 0x0000ff00U);
  put32(rgba, 100, 0x000000ffU);
  put32(rgba, 104, 0xff000000U);
  put32(rgba, 20, 16);
  rgba.resize(128U + 64U);
  rgba[128] = 30;
  rgba[129] = 20;
  rgba[130] = 10;
  rgba[131] = 40;
  const auto raw = pmxer::decodeImageBytes(rgba);
  assert(raw && raw.rgba[0] == 10 && raw.rgba[1] == 20 && raw.rgba[2] == 30 &&
         raw.rgba[3] == 40);

  auto extended = dds(0x30315844U, 28);
  extended.resize(156U);
  put32(extended, 128, 71U);
  put32(extended, 132, 3U);
  put32(extended, 140, 1U);
  extended[148] = 0x00;
  extended[149] = 0xf8;
  const auto extendedRed = pmxer::decodeImageBytes(extended);
  assert(extendedRed && extendedRed.rgba[0] == 255 &&
         extendedRed.rgba[3] == 255);

  auto bc4 = dds(0x31495441U, 8);
  bc4[128] = 255;
  const auto grayscale = pmxer::decodeImageBytes(bc4);
  assert(grayscale && grayscale.rgba[0] == 255 && grayscale.rgba[1] == 255 &&
         grayscale.rgba[2] == 255 && grayscale.rgba[3] == 255);

  auto bc5 = dds(0x32495441U, 16);
  bc5[128] = 255;
  bc5[136] = 128;
  const auto twoChannel = pmxer::decodeImageBytes(bc5);
  assert(twoChannel && twoChannel.rgba[0] == 255 && twoChannel.rgba[1] == 128 &&
         twoChannel.rgba[2] == 0 && twoChannel.rgba[3] == 255);

  auto bc7 = dds(0x30315844U, 36);
  put32(bc7, 128, 98U);
  put32(bc7, 132, 3U);
  put32(bc7, 140, 1U);
  bc7[148] = 0x10;
  const auto modern = pmxer::decodeImageBytes(bc7);
  assert(modern && modern.width == 4 && modern.height == 4 &&
         modern.rgba.size() == 64);

  put32(bc7, 140, 2U);
  const auto arrayTexture = pmxer::decodeImageBytes(bc7);
  assert(!arrayTexture &&
         arrayTexture.error == "unsupported DDS texture layout");
  return 0;
}
