#include "PreviewTextureFallbacks.hpp"

namespace pmxer {

ToonFallbackPixels makeSharedToonFallback(std::size_t /*index*/) noexcept {
    // Shared toon files are external MMD assets.  A dark procedural substitute
    // makes an otherwise valid model look uneditable, so the editor uses a
    // white no-op toon until the real asset is available.
    ToonFallbackPixels result{};
    result.fill(255U);
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
