#pragma once

#include "Selection.hpp"

#include <mmd/pmx.hpp>

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace pmxer {

enum class DeformMode : std::uint8_t { inactive, shape, pose, mix };
enum class PivotMode : std::uint8_t { median, active, origin };

struct VertexDelta {
    mmd::VertexHandle vertex{};
    mmd::Float3 offset{};
};

struct BoneDelta {
    mmd::BoneHandle bone{};
    mmd::Float3 translation{};
    mmd::Float4 rotation{0.0F, 0.0F, 0.0F, 1.0F};
};

struct MorphBlend {
    mmd::MorphHandle morph{};
    float weight{};
};

struct DragVertex {
    mmd::VertexHandle vertex{};
    mmd::Float3 position{};
};

struct DragBone {
    mmd::BoneHandle bone{};
    mmd::Float3 position{};
};

struct DeformSession {
    DeformMode mode{DeformMode::inactive};
    std::uint64_t sourceRevision{};
    std::vector<VertexDelta> vertices;
    std::vector<BoneDelta> bones;
    std::vector<DragVertex> dragVertices;
    std::vector<DragBone> dragBones;
    std::vector<MorphBlend> blends;
    PivotMode pivotMode{PivotMode::median};
    bool symmetryX{};
    float symmetryCenterX{};
    float symmetryFeather{0.02F};
    bool symmetrySwap{};
    bool duplicateCenterVertices{};
    bool dirty{};
    bool solo{};
    mmd::MorphHandle soloMorph{};
    mmd::MorphHandle operationMorph{};
    float morphScaleFactor{1.0F};
    std::int32_t materialIndex{-1};
    std::array<char, 128> morphSearch{};
    std::string captureName{"New Morph"};

    [[nodiscard]] bool active() const noexcept {
        return mode == DeformMode::shape || mode == DeformMode::pose;
    }

    void clearOverlay() {
        vertices.clear();
        bones.clear();
        dragVertices.clear();
        dragBones.clear();
        dirty = false;
    }

    void reset() {
        mode = DeformMode::inactive;
        sourceRevision = 0;
        clearOverlay();
        blends.clear();
        pivotMode = PivotMode::median;
        symmetryX = false;
        symmetryCenterX = 0.0F;
        symmetryFeather = 0.02F;
        symmetrySwap = false;
        duplicateCenterVertices = false;
        solo = false;
        soloMorph = {};
        operationMorph = {};
        morphScaleFactor = 1.0F;
        materialIndex = -1;
        morphSearch = {};
        captureName = "New Morph";
    }
};

} // namespace pmxer
