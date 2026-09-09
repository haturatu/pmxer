#include "DeformController.hpp"

#include "DocumentSession.hpp"
#include "../preview/PreviewController.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace pmxer {
namespace {

mmd::Float3 add(const mmd::Float3 &lhs, const mmd::Float3 &rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

mmd::Float3 subtract(const mmd::Float3 &lhs, const mmd::Float3 &rhs) {
    return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

mmd::Float3 transformPoint(const std::array<float, 16> &matrix, const mmd::Float3 &point) {
    return {
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
    };
}

std::array<float, 16> multiplyMatrix(const std::array<float, 16> &lhs,
                                     const std::array<float, 16> &rhs) {
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4U; ++column)
        for (std::size_t row = 0; row < 4U; ++row)
            for (std::size_t component = 0; component < 4U; ++component)
                result[column * 4U + row] += lhs[component * 4U + row] * rhs[column * 4U + component];
    return result;
}

std::array<float, 16> inverseAffine(const std::array<float, 16> &matrix) {
    const auto a00 = matrix[0];
    const auto a01 = matrix[4];
    const auto a02 = matrix[8];
    const auto a10 = matrix[1];
    const auto a11 = matrix[5];
    const auto a12 = matrix[9];
    const auto a20 = matrix[2];
    const auto a21 = matrix[6];
    const auto a22 = matrix[10];
    const auto determinant = a00 * (a11 * a22 - a12 * a21) -
                             a01 * (a10 * a22 - a12 * a20) +
                             a02 * (a10 * a21 - a11 * a20);
    if (std::abs(determinant) <= 1e-8F) {
        std::array<float, 16> identity{};
        identity[0] = identity[5] = identity[10] = identity[15] = 1.0F;
        return identity;
    }
    const auto inverseDeterminant = 1.0F / determinant;
    const auto i00 = (a11 * a22 - a12 * a21) * inverseDeterminant;
    const auto i01 = (a02 * a21 - a01 * a22) * inverseDeterminant;
    const auto i02 = (a01 * a12 - a02 * a11) * inverseDeterminant;
    const auto i10 = (a12 * a20 - a10 * a22) * inverseDeterminant;
    const auto i11 = (a00 * a22 - a02 * a20) * inverseDeterminant;
    const auto i12 = (a02 * a10 - a00 * a12) * inverseDeterminant;
    const auto i20 = (a10 * a21 - a11 * a20) * inverseDeterminant;
    const auto i21 = (a01 * a20 - a00 * a21) * inverseDeterminant;
    const auto i22 = (a00 * a11 - a01 * a10) * inverseDeterminant;
    std::array<float, 16> result{};
    result[0] = i00;
    result[4] = i01;
    result[8] = i02;
    result[1] = i10;
    result[5] = i11;
    result[9] = i12;
    result[2] = i20;
    result[6] = i21;
    result[10] = i22;
    result[15] = 1.0F;
    result[12] = -(i00 * matrix[12] + i01 * matrix[13] + i02 * matrix[14]);
    result[13] = -(i10 * matrix[12] + i11 * matrix[13] + i12 * matrix[14]);
    result[14] = -(i20 * matrix[12] + i21 * matrix[13] + i22 * matrix[14]);
    return result;
}

mmd::Float4 multiplyQuaternion(const mmd::Float4 &lhs, const mmd::Float4 &rhs) {
    return {
        lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
        lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
        lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2],
    };
}

mmd::Float4 quaternionFromMatrix(const std::array<float, 16> &matrix) {
    const auto trace = matrix[0] + matrix[5] + matrix[10];
    mmd::Float4 result{};
    if (trace > 0.0F) {
        const auto factor = 0.5F / std::sqrt(trace + 1.0F);
        result = {(matrix[6] - matrix[9]) * factor,
                  (matrix[8] - matrix[2]) * factor,
                  (matrix[1] - matrix[4]) * factor,
                  0.25F / factor};
    } else if (matrix[0] > matrix[5] && matrix[0] > matrix[10]) {
        const auto factor = 2.0F * std::sqrt(std::max(1.0F + matrix[0] - matrix[5] - matrix[10], 1e-8F));
        result = {0.25F * factor, (matrix[4] + matrix[1]) / factor,
                  (matrix[8] + matrix[2]) / factor, (matrix[6] - matrix[9]) / factor};
    } else if (matrix[5] > matrix[10]) {
        const auto factor = 2.0F * std::sqrt(std::max(1.0F + matrix[5] - matrix[0] - matrix[10], 1e-8F));
        result = {(matrix[4] + matrix[1]) / factor, 0.25F * factor,
                  (matrix[9] + matrix[6]) / factor, (matrix[8] - matrix[2]) / factor};
    } else {
        const auto factor = 2.0F * std::sqrt(std::max(1.0F + matrix[10] - matrix[0] - matrix[5], 1e-8F));
        result = {(matrix[8] + matrix[2]) / factor, (matrix[9] + matrix[6]) / factor,
                  0.25F * factor, (matrix[1] - matrix[4]) / factor};
    }
    float length{};
    for (const auto component : result)
        length += component * component;
    if (length <= 1e-8F)
        return {0.0F, 0.0F, 0.0F, 1.0F};
    for (auto &component : result)
        component /= std::sqrt(length);
    return result;
}

VertexDelta *findVertexDelta(DeformSession &deform, mmd::VertexHandle handle) {
    const auto found = std::find_if(deform.vertices.begin(), deform.vertices.end(),
                                    [&](const auto &delta) { return delta.vertex == handle; });
    return found == deform.vertices.end() ? nullptr : &*found;
}

BoneDelta *findBoneDelta(DeformSession &deform, mmd::BoneHandle handle) {
    const auto found = std::find_if(deform.bones.begin(), deform.bones.end(),
                                    [&](const auto &delta) { return delta.bone == handle; });
    return found == deform.bones.end() ? nullptr : &*found;
}

void setVertexDelta(DeformSession &deform, mmd::VertexHandle handle,
                    const mmd::Float3 &offset) {
    if (auto *delta = findVertexDelta(deform, handle))
        delta->offset = offset;
    else
        deform.vertices.push_back({handle, offset});
}

void setBoneDelta(DeformSession &deform, mmd::BoneHandle handle,
                  const mmd::Float3 &translation, const mmd::Float4 &rotation) {
    if (auto *delta = findBoneDelta(deform, handle)) {
        delta->translation = translation;
        delta->rotation = rotation;
    } else {
        deform.bones.push_back({handle, translation, rotation});
    }
}

using SymmetryCell = std::array<std::int64_t, 3>;

struct SymmetryCellHash {
    std::size_t operator()(const SymmetryCell &cell) const noexcept {
        std::size_t result = 0xcbf29ce484222325ULL;
        for (const auto value : cell) {
            result ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ULL +
                      (result << 6U) + (result >> 2U);
        }
        return result;
    }
};

SymmetryCell symmetryCell(const mmd::Float3 &position, float size) {
    return {static_cast<std::int64_t>(std::llround(position[0] / size)),
            static_cast<std::int64_t>(std::llround(position[1] / size)),
            static_cast<std::int64_t>(std::llround(position[2] / size))};
}

std::optional<std::size_t> vertexIndex(const DocumentSession &session,
                                       mmd::VertexHandle handle) {
    const auto *vertex = session.document.resolve(handle);
    if (vertex == nullptr)
        return std::nullopt;
    return static_cast<std::size_t>(vertex - session.document.model().vertices.data());
}

} // namespace

mmd::Float3 deformVertexPosition(const DocumentSession &session, mmd::VertexHandle vertex) {
    const auto *value = session.document.resolve(vertex);
    if (value == nullptr)
        return {};
    const auto found = std::find_if(session.deform.vertices.begin(), session.deform.vertices.end(),
                                    [&](const auto &delta) { return delta.vertex == vertex; });
    return found == session.deform.vertices.end() ? value->position : add(value->position, found->offset);
}

mmd::Float3 deformBonePosition(const DocumentSession &session, mmd::BoneHandle bone) {
    const auto *value = session.document.resolve(bone);
    if (value == nullptr)
        return {};
    const auto found = std::find_if(session.deform.bones.begin(), session.deform.bones.end(),
                                    [&](const auto &delta) { return delta.bone == bone; });
    return found == session.deform.bones.end() ? value->position : add(value->position, found->translation);
}

void beginDeformGizmoDrag(DocumentSession &session,
                          const std::array<float, 16> &startGizmoMatrix) {
    session.deform.dragStartGizmoMatrix = startGizmoMatrix;
    session.deform.dragVertices.clear();
    session.deform.dragBones.clear();
    if (session.deform.mode == DeformMode::shape) {
        if (session.deform.symmetryX)
            rebuildSymmetryCache(session);
        for (const auto &item : session.selection.items()) {
            if (item.kind != SelectionKind::vertex)
                continue;
            const auto handle = selectionHandle<mmd::VertexTag>(session.document, item);
            if (session.document.resolve(handle) != nullptr)
                session.deform.dragVertices.push_back({handle, deformVertexPosition(session, handle)});
        }
    } else if (session.deform.mode == DeformMode::pose) {
        for (const auto &item : session.selection.items()) {
            if (item.kind != SelectionKind::bone)
                continue;
            const auto handle = selectionHandle<mmd::BoneTag>(session.document, item);
            if (session.document.resolve(handle) != nullptr)
                session.deform.dragBones.push_back({handle, deformBonePosition(session, handle),
                                                    findBoneDelta(session.deform, handle) == nullptr
                                                        ? mmd::Float4{0.0F, 0.0F, 0.0F, 1.0F}
                                                        : findBoneDelta(session.deform, handle)->rotation});
        }
    }
}

void rebuildSymmetryCache(DocumentSession &session) {
    auto &deform = session.deform;
    const auto tolerance = std::max(std::abs(deform.symmetryTolerance), 1e-4F);
    deform.symmetryCacheRevision = session.revision;
    deform.symmetryCacheCenterX = deform.symmetryCenterX;
    deform.symmetryCacheTolerance = tolerance;
    deform.symmetryMirrorIndices.assign(session.document.model().vertices.size(), -1);

    std::unordered_map<SymmetryCell, std::vector<std::size_t>, SymmetryCellHash> buckets;
    buckets.reserve(session.document.model().vertices.size());
    for (std::size_t index = 0; index < session.document.model().vertices.size(); ++index)
        buckets[symmetryCell(session.document.model().vertices[index].position, tolerance)].push_back(index);

    const auto toleranceSquared = tolerance * tolerance;
    for (std::size_t source = 0; source < session.document.model().vertices.size(); ++source) {
        const auto &position = session.document.model().vertices[source].position;
        const mmd::Float3 reflected{2.0F * deform.symmetryCenterX - position[0], position[1], position[2]};
        const auto center = symmetryCell(reflected, tolerance);
        std::size_t nearest{};
        float nearestDistance = std::numeric_limits<float>::max();
        bool found = false;
        for (std::int64_t x = -1; x <= 1; ++x) {
            for (std::int64_t y = -1; y <= 1; ++y) {
                for (std::int64_t z = -1; z <= 1; ++z) {
                    const SymmetryCell cell{center[0] + x, center[1] + y, center[2] + z};
                    const auto bucket = buckets.find(cell);
                    if (bucket == buckets.end())
                        continue;
                    for (const auto candidate : bucket->second) {
                        const auto &candidatePosition = session.document.model().vertices[candidate].position;
                        const auto dx = candidatePosition[0] - reflected[0];
                        const auto dy = candidatePosition[1] - reflected[1];
                        const auto dz = candidatePosition[2] - reflected[2];
                        const auto distance = dx * dx + dy * dy + dz * dz;
                        if (distance <= toleranceSquared && distance < nearestDistance) {
                            nearest = candidate;
                            nearestDistance = distance;
                            found = true;
                        }
                    }
                }
            }
        }
        if (found)
            deform.symmetryMirrorIndices[source] = static_cast<std::int32_t>(nearest);
    }
}

void updateDeformGizmoDrag(DocumentSession &session,
                           const std::array<float, 16> &currentGizmoMatrix) {
    auto &deform = session.deform;
    const auto symmetryTolerance = std::max(std::abs(deform.symmetryTolerance), 1e-4F);
    const auto dragMatrix = multiplyMatrix(currentGizmoMatrix, inverseAffine(deform.dragStartGizmoMatrix));
    if (deform.mode == DeformMode::shape) {
        if (deform.symmetryX &&
            (deform.symmetryCacheRevision != session.revision ||
             deform.symmetryCacheCenterX != deform.symmetryCenterX ||
             deform.symmetryCacheTolerance != symmetryTolerance ||
             deform.symmetryMirrorIndices.size() != session.document.model().vertices.size()))
            rebuildSymmetryCache(session);
        for (const auto &drag : deform.dragVertices) {
            const auto transformed = transformPoint(dragMatrix, drag.position);
            const auto *base = session.document.resolve(drag.vertex);
            if (base == nullptr)
                continue;
            setVertexDelta(deform, drag.vertex, subtract(transformed, base->position));
        }
        if (deform.symmetryX) {
            for (const auto &drag : deform.dragVertices) {
                const auto *base = session.document.resolve(drag.vertex);
                const auto sourceIndex = vertexIndex(session, drag.vertex);
                if (base == nullptr || !sourceIndex || *sourceIndex >= deform.symmetryMirrorIndices.size())
                    continue;
                const auto transformed = transformPoint(dragMatrix, drag.position);
                auto offset = subtract(transformed, base->position);
                const auto mirrorIndex = deform.symmetryMirrorIndices[*sourceIndex];
                if (mirrorIndex == static_cast<std::int32_t>(*sourceIndex)) {
                    // A center-line vertex cannot move across the symmetry
                    // plane. Preserve its tangential movement only.
                    offset[0] = 0.0F;
                    setVertexDelta(deform, drag.vertex, offset);
                } else if (mirrorIndex >= 0) {
                    const auto mirror = session.document.vertexHandle(static_cast<std::size_t>(mirrorIndex));
                    setVertexDelta(deform, mirror,
                                   {deform.symmetrySwapSides ? offset[0] : -offset[0], offset[1], offset[2]});
                }
            }
        }
    } else if (deform.mode == DeformMode::pose) {
        const auto rotation = quaternionFromMatrix(dragMatrix);
        for (const auto &drag : deform.dragBones) {
            const auto transformed = transformPoint(dragMatrix, drag.position);
            const auto *base = session.document.resolve(drag.bone);
            if (base != nullptr)
                setBoneDelta(deform, drag.bone, subtract(transformed, base->position),
                             multiplyQuaternion(drag.rotation, rotation));
        }
    }
    deform.dirty = true;
}

std::vector<mmd::PmxMorphOffset> vertexMorphOffsets(const DocumentSession &session) {
    std::vector<mmd::PmxMorphOffset> offsets;
    offsets.reserve(session.deform.vertices.size());
    for (const auto &delta : session.deform.vertices) {
        const auto *vertex = session.document.resolve(delta.vertex);
        if (vertex == nullptr)
            continue;
        mmd::PmxMorphOffset offset;
        offset.index = static_cast<std::int32_t>(vertex - session.document.model().vertices.data());
        offset.vector3 = delta.offset;
        offsets.push_back(offset);
    }
    return offsets;
}

std::vector<mmd::PmxMorphOffset> boneMorphOffsets(const DocumentSession &session) {
    std::vector<mmd::PmxMorphOffset> offsets;
    offsets.reserve(session.deform.bones.size());
    for (const auto &delta : session.deform.bones) {
        const auto *bone = session.document.resolve(delta.bone);
        if (bone == nullptr)
            continue;
        mmd::PmxMorphOffset offset;
        offset.index = static_cast<std::int32_t>(bone - session.document.model().bones.data());
        offset.vector3 = delta.translation;
        offset.vector4 = delta.rotation;
        offsets.push_back(offset);
    }
    return offsets;
}

void refreshDeformPreview(DocumentSession &session) {
    if (session.deform.active() && session.preview.controller &&
        (session.deform.mode == DeformMode::shape || session.deform.mode == DeformMode::pose)) {
        session.preview.controller->setVertexPreview(
            session.deform.mode == DeformMode::shape ? vertexMorphOffsets(session)
                                                     : std::vector<mmd::PmxMorphOffset>{});
        session.preview.controller->setBonePreview(
            session.deform.mode == DeformMode::pose ? boneMorphOffsets(session)
                                                    : std::vector<mmd::PmxMorphOffset>{});
        session.preview.baseFrame = session.preview.controller->evaluate();
    }
    if (!session.preview.baseFrame)
        return;
    session.preview.frame = *session.preview.baseFrame;
    ++session.preview.frameRevision;
    session.ui.previewFrame = &*session.preview.frame;
}

} // namespace pmxer
