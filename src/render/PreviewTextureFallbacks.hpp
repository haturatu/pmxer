#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pmxer {

using ToonFallbackPixels = std::array<std::uint8_t, 64U * 4U>;

[[nodiscard]] ToonFallbackPixels makeSharedToonFallback(std::size_t index) noexcept;
[[nodiscard]] ToonFallbackPixels makeNeutralToonFallback() noexcept;

} // namespace pmxer
