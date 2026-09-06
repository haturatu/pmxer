#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pmxer {

struct DecodedImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return width != 0 && height != 0 && !rgba.empty() && error.empty();
    }
};

[[nodiscard]] DecodedImage decodeImage(const std::filesystem::path &path);
[[nodiscard]] DecodedImage decodeImageBytes(const std::vector<std::uint8_t> &bytes);

} // namespace pmxer
