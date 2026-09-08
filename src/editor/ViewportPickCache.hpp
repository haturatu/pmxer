#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pmxer {

struct ProjectedVertex {
    float x{};
    float y{};
    float depth{};
    bool inFront{};
};

struct ViewportPickCache {
    static constexpr std::size_t gridWidth = 64;
    static constexpr std::size_t gridHeight = 64;

    std::uint64_t revision{};
    std::uint64_t frameRevision{};
    float cameraYaw{};
    float cameraPitch{};
    float cameraDistance{};
    float cameraTargetX{};
    float cameraTargetY{};
    float cameraTargetZ{};
    bool orthographic{};
    float originX{};
    float originY{};
    float width{};
    float height{};
    std::vector<ProjectedVertex> vertices;
    std::vector<std::uint32_t> faceMaterial;
    std::vector<std::vector<std::uint32_t>> grid;

    void clear() {
        vertices.clear();
        faceMaterial.clear();
        grid.clear();
    }
};

} // namespace pmxer
