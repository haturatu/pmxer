#include "ImageDecoder.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>

#define BCDEC_IMPLEMENTATION
#include <bcdec.h>

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>

namespace pmxer {
namespace {

constexpr std::size_t maxDecodedBytes = 256U * 1024U * 1024U;

constexpr std::uint32_t fourCc(char a, char b, char c, char d) {
  return static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8U) |
         (static_cast<std::uint32_t>(c) << 16U) |
         (static_cast<std::uint32_t>(d) << 24U);
}

std::uint32_t read32(const std::vector<std::uint8_t> &data,
                     std::size_t offset) {
  return static_cast<std::uint32_t>(data[offset]) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

std::array<std::uint8_t, 4> color565(std::uint16_t value) {
  return {static_cast<std::uint8_t>(((value >> 11U) & 31U) * 255U / 31U),
          static_cast<std::uint8_t>(((value >> 5U) & 63U) * 255U / 63U),
          static_cast<std::uint8_t>((value & 31U) * 255U / 31U), 255};
}

std::array<std::array<std::uint8_t, 4>, 4> colorTable(const std::uint8_t *block,
                                                      bool allowTransparent) {
  const auto first = static_cast<std::uint16_t>(block[0] | (block[1] << 8U));
  const auto second = static_cast<std::uint16_t>(block[2] | (block[3] << 8U));
  std::array<std::array<std::uint8_t, 4>, 4> colors{color565(first),
                                                    color565(second)};
  if (first > second || !allowTransparent) {
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      colors[2][channel] = static_cast<std::uint8_t>(
          (2U * colors[0][channel] + colors[1][channel]) / 3U);
      colors[3][channel] = static_cast<std::uint8_t>(
          (colors[0][channel] + 2U * colors[1][channel]) / 3U);
    }
    colors[2][3] = colors[3][3] = 255;
  } else {
    for (std::size_t channel = 0; channel < 3U; ++channel)
      colors[2][channel] = static_cast<std::uint8_t>(
          (colors[0][channel] + colors[1][channel]) / 2U);
    colors[2][3] = 255;
    colors[3] = {0, 0, 0, 0};
  }
  return colors;
}

std::array<std::uint8_t, 8> alphaTable(const std::uint8_t *block) {
  std::array<std::uint8_t, 8> alpha{block[0], block[1]};
  if (alpha[0] > alpha[1]) {
    for (unsigned i = 1; i <= 6; ++i)
      alpha[i + 1] =
          static_cast<std::uint8_t>(((7U - i) * alpha[0] + i * alpha[1]) / 7U);
  } else {
    for (unsigned i = 1; i <= 4; ++i)
      alpha[i + 1] =
          static_cast<std::uint8_t>(((5U - i) * alpha[0] + i * alpha[1]) / 5U);
    alpha[6] = 0;
    alpha[7] = 255;
  }
  return alpha;
}

void writePixel(DecodedImage &image, std::uint32_t x, std::uint32_t y,
                const std::array<std::uint8_t, 4> &color) {
  if (x >= image.width || y >= image.height)
    return;
  const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
  std::copy(color.begin(), color.end(),
            image.rgba.begin() + static_cast<std::ptrdiff_t>(offset));
}

DecodedImage decodeDds(const std::vector<std::uint8_t> &bytes) {
  DecodedImage image;
  if (bytes.size() < 128 || read32(bytes, 0) != fourCc('D', 'D', 'S', ' ') ||
      read32(bytes, 4) != 124 || read32(bytes, 76) != 32) {
    image.error = "invalid DDS header";
    return image;
  }
  image.height = read32(bytes, 12);
  image.width = read32(bytes, 16);
  if (image.width == 0 || image.height == 0 || image.width > 16384 ||
      image.height > 16384 ||
      static_cast<std::uint64_t>(image.width) * image.height >
          maxDecodedBytes / 4U) {
    image.error = "invalid DDS dimensions";
    return image;
  }
  const auto format = read32(bytes, 84);
  bool bc1 = format == fourCc('D', 'X', 'T', '1');
  bool bc2 = format == fourCc('D', 'X', 'T', '3');
  bool bc3 = format == fourCc('D', 'X', 'T', '5');
  bool bc4 = format == fourCc('A', 'T', 'I', '1') ||
             format == fourCc('B', 'C', '4', 'U');
  bool bc5 = format == fourCc('A', 'T', 'I', '2') ||
             format == fourCc('B', 'C', '5', 'U');
  bool bc7 = false;
  std::size_t dataOffset = 128U;
  if (format == fourCc('D', 'X', '1', '0')) {
    if (bytes.size() < 148U) {
      image.error = "truncated DDS extended header";
      image.rgba.clear();
      return image;
    }
    const auto extendedFormat = read32(bytes, 128);
    bc1 = extendedFormat == 71U || extendedFormat == 72U;
    bc2 = extendedFormat == 74U || extendedFormat == 75U;
    bc3 = extendedFormat == 77U || extendedFormat == 78U;
    bc4 = extendedFormat == 80U;
    bc5 = extendedFormat == 83U;
    bc7 = extendedFormat == 98U || extendedFormat == 99U;
    const auto resourceDimension = read32(bytes, 132);
    const auto miscellaneousFlags = read32(bytes, 136);
    const auto arraySize = read32(bytes, 140);
    if (resourceDimension != 3U || arraySize != 1U ||
        (miscellaneousFlags & 0x4U) != 0U) {
      image.error = "unsupported DDS texture layout";
      image.rgba.clear();
      return image;
    }
    dataOffset = 148U;
  } else if (read32(bytes, 112) != 0U) {
    image.error = "unsupported DDS texture layout";
    image.rgba.clear();
    return image;
  }
  const auto pixelFlags = read32(bytes, 80);
  const auto bitsPerPixel = read32(bytes, 88);
  const bool uncompressed = (pixelFlags & 0x40U) != 0U &&
                            (bitsPerPixel == 24U || bitsPerPixel == 32U);
  if (!bc1 && !bc2 && !bc3 && !bc4 && !bc5 && !bc7 && !uncompressed) {
    image.error = "unsupported DDS pixel format";
    image.rgba.clear();
    return image;
  }
  if (uncompressed) {
    const auto bytesPerPixel = static_cast<std::size_t>(bitsPerPixel / 8U);
    const auto minimumPitch =
        static_cast<std::size_t>(image.width) * bytesPerPixel;
    const auto declaredPitch = static_cast<std::size_t>(read32(bytes, 20));
    const auto rowPitch =
        declaredPitch >= minimumPitch ? declaredPitch : minimumPitch;
    if (static_cast<std::uint64_t>(rowPitch) * image.height >
        bytes.size() - dataOffset) {
      image.error = "truncated DDS image";
      return image;
    }
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height *
                      4U);
    const std::array<std::uint32_t, 4> masks{
        read32(bytes, 92), read32(bytes, 96), read32(bytes, 100),
        read32(bytes, 104)};
    const auto channel = [](std::uint32_t value, std::uint32_t mask,
                            std::uint8_t fallback) {
      if (mask == 0)
        return fallback;
      const auto shift = std::countr_zero(mask);
      const auto maximum = mask >> shift;
      return static_cast<std::uint8_t>(((value & mask) >> shift) * 255U /
                                       maximum);
    };
    for (std::uint32_t y = 0; y < image.height; ++y) {
      const auto *row =
          bytes.data() + dataOffset + static_cast<std::size_t>(y) * rowPitch;
      for (std::uint32_t x = 0; x < image.width; ++x) {
        std::uint32_t value{};
        for (std::size_t byte = 0; byte < bytesPerPixel; ++byte)
          value |= static_cast<std::uint32_t>(
                       row[static_cast<std::size_t>(x) * bytesPerPixel + byte])
                   << (8U * byte);
        writePixel(image, x, y,
                   {channel(value, masks[0], 0), channel(value, masks[1], 0),
                    channel(value, masks[2], 0),
                    channel(value, masks[3], 255)});
      }
    }
    return image;
  }
  const std::size_t blockSize = bc1 || bc4 ? 8U : 16U;
  const auto blocksWide = (image.width + 3U) / 4U;
  const auto blocksHigh = (image.height + 3U) / 4U;
  const auto blockCount = static_cast<std::uint64_t>(blocksWide) * blocksHigh;
  if (blockCount > (bytes.size() - dataOffset) / blockSize) {
    image.error = "truncated DDS image";
    return image;
  }
  image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
  const std::uint8_t *block = bytes.data() + dataOffset;
  for (std::uint32_t by = 0; by < blocksHigh; ++by) {
    for (std::uint32_t bx = 0; bx < blocksWide; ++bx, block += blockSize) {
      if (bc4) {
        std::array<std::uint8_t, 16> decoded{};
        bcdec_bc4(block, decoded.data(), 4);
        for (unsigned pixel = 0; pixel < 16; ++pixel) {
          const auto value = decoded[pixel];
          writePixel(image, bx * 4U + pixel % 4U, by * 4U + pixel / 4U,
                     {value, value, value, 255});
        }
        continue;
      }
      if (bc5) {
        std::array<std::uint8_t, 32> decoded{};
        bcdec_bc5(block, decoded.data(), 8);
        for (unsigned pixel = 0; pixel < 16; ++pixel)
          writePixel(image, bx * 4U + pixel % 4U, by * 4U + pixel / 4U,
                     {decoded[pixel * 2U], decoded[pixel * 2U + 1U], 0, 255});
        continue;
      }
      if (bc7) {
        std::array<std::uint8_t, 64> decoded{};
        bcdec_bc7(block, decoded.data(), 16);
        for (unsigned pixel = 0; pixel < 16; ++pixel)
          writePixel(image, bx * 4U + pixel % 4U, by * 4U + pixel / 4U,
                     {decoded[pixel * 4U], decoded[pixel * 4U + 1U],
                      decoded[pixel * 4U + 2U], decoded[pixel * 4U + 3U]});
        continue;
      }
      const auto *colorBlock = block + (bc1 ? 0U : 8U);
      const auto colors = colorTable(colorBlock, bc1);
      const auto colorBits =
          static_cast<std::uint32_t>(colorBlock[4]) |
          (static_cast<std::uint32_t>(colorBlock[5]) << 8U) |
          (static_cast<std::uint32_t>(colorBlock[6]) << 16U) |
          (static_cast<std::uint32_t>(colorBlock[7]) << 24U);
      const auto alpha =
          bc3 ? alphaTable(block) : std::array<std::uint8_t, 8>{};
      std::uint64_t alphaBits = 0;
      if (bc3)
        for (unsigned i = 0; i < 6; ++i)
          alphaBits |= static_cast<std::uint64_t>(block[i + 2]) << (8U * i);
      for (unsigned pixel = 0; pixel < 16; ++pixel) {
        auto value = colors[(colorBits >> (2U * pixel)) & 3U];
        if (bc2) {
          const auto nibble = static_cast<std::uint8_t>(
              (block[pixel / 2U] >> (4U * (pixel & 1U))) & 15U);
          value[3] = static_cast<std::uint8_t>(nibble * 17U);
        } else if (bc3) {
          value[3] = alpha[(alphaBits >> (3U * pixel)) & 7U];
        }
        writePixel(image, bx * 4U + pixel % 4U, by * 4U + pixel / 4U, value);
      }
    }
  }
  return image;
}

} // namespace

DecodedImage decodeImageBytes(const std::vector<std::uint8_t> &bytes) {
  if (bytes.size() >= 4 && read32(bytes, 0) == fourCc('D', 'D', 'S', ' '))
    return decodeDds(bytes);
  if (bytes.empty() ||
      bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return {{}, {}, {}, "empty or oversized image"};
  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                            &width, &height, &channels) == 0 ||
      width <= 0 || height <= 0)
    return {{},
            {},
            {},
            stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                             : "image decode failed"};
  if (width > 16384 || height > 16384 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
          maxDecodedBytes / 4U) {
    return {{}, {}, {}, "invalid image dimensions"};
  }
  auto *pixels =
      stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                            &width, &height, &channels, 4);
  if (pixels == nullptr)
    return {{},
            {},
            {},
            stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                             : "image decode failed"};
  DecodedImage image;
  image.width = static_cast<std::uint32_t>(width);
  image.height = static_cast<std::uint32_t>(height);
  image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(height) * 4U);
  stbi_image_free(pixels);
  return image;
}

DecodedImage decodeImage(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream)
    return {{}, {}, {}, "image file cannot be opened"};
  const auto end = stream.tellg();
  const auto fileSize =
      static_cast<std::uint64_t>(static_cast<std::streamoff>(end));
  if (end <= std::streampos{0} || fileSize > 1024ULL * 1024ULL * 1024ULL)
    return {{}, {}, {}, "image file has an invalid size"};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
    return {{}, {}, {}, "image file could not be read"};
  return decodeImageBytes(bytes);
}

} // namespace pmxer
