#include "PreviewTextureFallbacks.hpp"

namespace pmxer {

ToonFallbackPixels makeSharedToonFallback(std::size_t index) noexcept {
    constexpr std::array<std::array<std::uint8_t, 3>, 10> shadows{{
        {52, 52, 56}, {64, 51, 51}, {51, 59, 68}, {58, 51, 66}, {50, 65, 56},
        {69, 60, 47}, {47, 64, 68}, {67, 48, 60}, {58, 58, 47}, {44, 44, 48},
    }};
    const auto shadow = shadows[index % shadows.size()];
    ToonFallbackPixels result{};
    for (std::size_t row = 0; row < 64U; ++row) {
        const auto eased = row * row;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const auto range = 255U - shadow[channel];
            result[row * 4U + channel] = static_cast<std::uint8_t>(
                255U - range * eased / (63U * 63U));
        }
        result[row * 4U + 3U] = 255;
    }
    return result;
}

ToonFallbackPixels makeNeutralToonFallback() noexcept {
    ToonFallbackPixels result{};
    for (std::size_t row = 0; row < 64U; ++row) {
        const auto value = static_cast<std::uint8_t>(255U - row * 159U / 63U);
        result[row * 4U] = value;
        result[row * 4U + 1U] = value;
        result[row * 4U + 2U] = value;
        result[row * 4U + 3U] = 255;
    }
    return result;
}

} // namespace pmxer
