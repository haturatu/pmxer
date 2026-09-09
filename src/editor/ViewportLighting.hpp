#pragma once

#include <array>
#include <cstdint>

namespace pmxer {

enum class ViewportShadingMode : std::uint8_t { mmd, neutral, unlit };

struct ViewportLightingSettings {
    ViewportShadingMode mode{ViewportShadingMode::neutral};
    float lightYaw{-0.55F};
    float lightPitch{0.75F};
    float lightIntensity{1.0F};
    float ambientIntensity{0.45F};
    float exposure{0.5F};
    float toonStrength{0.35F};
    float specularStrength{0.7F};
    float sphereStrength{0.5F};
    std::array<float, 4> background{0.055F, 0.065F, 0.08F, 1.0F};
};

} // namespace pmxer
