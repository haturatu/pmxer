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

inline void applyViewportShadingPreset(ViewportLightingSettings &settings,
                                        ViewportShadingMode mode) noexcept {
    settings.mode = mode;
    switch (mode) {
    case ViewportShadingMode::mmd:
        settings.lightIntensity = 0.60F;
        settings.ambientIntensity = 1.0F;
        settings.exposure = 0.0F;
        settings.toonStrength = 1.0F;
        settings.specularStrength = 1.0F;
        settings.sphereStrength = 1.0F;
        break;
    case ViewportShadingMode::neutral:
        settings.lightIntensity = 1.0F;
        settings.ambientIntensity = 0.45F;
        settings.exposure = 0.5F;
        settings.toonStrength = 0.35F;
        settings.specularStrength = 0.7F;
        settings.sphereStrength = 0.5F;
        break;
    case ViewportShadingMode::unlit:
        break;
    }
}

} // namespace pmxer
