#pragma once

#include "Selection.hpp"

#include <mmd/pmx.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace pmxer {

enum class DeformMode : std::uint8_t { inactive, shape, pose };
enum class TransformViewTab : std::uint8_t { vertex, bone, morph };
enum class PivotMode : std::uint8_t { median, active, origin };
enum class MirrorDriverSide : std::uint8_t { negativeX, positiveX };

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
    mmd::Float3 translation{};
    mmd::Float4 startWorldRotation{0.0F, 0.0F, 0.0F, 1.0F};
    mmd::Float4 startOverlayRotation{0.0F, 0.0F, 0.0F, 1.0F};
    mmd::Float4 preOverlayWorldRotation{0.0F, 0.0F, 0.0F, 1.0F};
    mmd::Float4 parentWorldRotation{0.0F, 0.0F, 0.0F, 1.0F};
};

struct MirrorPositionGroup {
    std::vector<std::size_t> source;
    std::vector<std::size_t> target;
};

struct DeformSession {
    DeformMode mode{DeformMode::inactive};
    TransformViewTab tab{TransformViewTab::vertex};
    bool tabActivated{};
    TransformViewTab activatedTab{TransformViewTab::vertex};
    // The panel can be closed without discarding an in-progress edit. Keep
    // this separate from mode so a closed panel cannot capture viewport edits.
    bool engaged{};
    bool suspended{};
    std::uint64_t sourceRevision{};
    std::vector<VertexDelta> vertices;
    std::vector<BoneDelta> bones;
    std::vector<DragVertex> dragVertices;
    std::vector<DragBone> dragBones;
    std::array<float, 16> dragStartGizmoMatrix{};
    PivotMode pivotMode{PivotMode::median};
    bool symmetryX{};
    float symmetryCenterX{};
    float symmetryTolerance{0.02F};
    float symmetryDuplicateEpsilon{1e-6F};
    bool symmetrySwapSides{};
    MirrorDriverSide mirrorDriverSide{MirrorDriverSide::negativeX};
    float sideSplitCenterX{};
    float sideSplitFeather{0.02F};
    bool sideSplitSwapSides{};
    bool sideSplitDuplicateCenterVertices{};
    bool dirty{};
    mmd::MorphHandle operationMorph{};
    float morphScaleFactor{1.0F};
    std::int32_t materialIndex{-1};
    std::array<char, 128> morphSearch{};
    std::string vertexCaptureName{"New Vertex Morph"};
    std::string boneCaptureName{"New Bone Morph"};
    std::string groupCaptureName{"New Group Morph"};
    bool confirmBakeReverse{};
    mmd::MorphHandle bakeReverseMorph{};
    std::size_t bakeReverseReferenceCount{};
    bool confirmVertexBake{};
    std::vector<std::uint8_t> vertexBakeIgnoredTypes;
    std::uint64_t symmetryCacheRevision{std::numeric_limits<std::uint64_t>::max()};
    float symmetryCacheCenterX{};
    float symmetryCacheTolerance{};
    float symmetryCacheDuplicateEpsilon{};
    std::vector<std::int32_t> symmetryMirrorGroupIndices;
    std::vector<MirrorPositionGroup> symmetryMirrorGroups;

    [[nodiscard]] bool active() const noexcept {
        return engaged && !suspended && (mode == DeformMode::shape || mode == DeformMode::pose);
    }

    void clearOverlay() {
        clearVertexOverlay();
        clearBoneOverlay();
    }

    void clearVertexOverlay() {
        vertices.clear();
        dragVertices.clear();
        dirty = !bones.empty();
    }

    void clearBoneOverlay() {
        bones.clear();
        dragBones.clear();
        dirty = !vertices.empty();
    }

    void reset() {
        mode = DeformMode::inactive;
        tab = TransformViewTab::vertex;
        tabActivated = false;
        activatedTab = TransformViewTab::vertex;
        engaged = false;
        suspended = false;
        sourceRevision = 0;
        clearOverlay();
        pivotMode = PivotMode::median;
        symmetryX = false;
        symmetryCenterX = 0.0F;
        symmetryTolerance = 0.02F;
        symmetryDuplicateEpsilon = 1e-6F;
        symmetrySwapSides = false;
        mirrorDriverSide = MirrorDriverSide::negativeX;
        sideSplitCenterX = 0.0F;
        sideSplitFeather = 0.02F;
        sideSplitSwapSides = false;
        sideSplitDuplicateCenterVertices = false;
        operationMorph = {};
        morphScaleFactor = 1.0F;
        materialIndex = -1;
        morphSearch = {};
        vertexCaptureName = "New Vertex Morph";
        boneCaptureName = "New Bone Morph";
        groupCaptureName = "New Group Morph";
        confirmBakeReverse = false;
        bakeReverseMorph = {};
        bakeReverseReferenceCount = 0;
        confirmVertexBake = false;
        vertexBakeIgnoredTypes.clear();
        symmetryCacheRevision = std::numeric_limits<std::uint64_t>::max();
        symmetryCacheCenterX = 0.0F;
        symmetryCacheTolerance = 0.0F;
        symmetryCacheDuplicateEpsilon = 0.0F;
        symmetryMirrorGroupIndices.clear();
        symmetryMirrorGroups.clear();
        dragStartGizmoMatrix = {};
    }
};

} // namespace pmxer
