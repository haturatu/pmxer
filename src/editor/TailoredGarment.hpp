#pragma once
#include "AiEditExecutor.hpp"

namespace pmxer::ai {
struct GarmentGeometry {
    std::vector<mmd::PmxVertex> vertices;
    struct Triangle {
        std::size_t a{}, b{}, c{}, material{};
    };
    std::vector<Triangle> triangles;
    std::vector<mmd::PmxMaterial> materials;
};
// Local, deterministic construction; no assets, credentials, or inference services.
[[nodiscard]] GarmentGeometry makeTailoredDress(const DocumentSession &, const EditRegion &,
                                                const Operation &);
[[nodiscard]] EditTarget suggestOutfitTarget(const DocumentSession &, const EditRegion &);
} // namespace pmxer::ai
