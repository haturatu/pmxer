#include "DeformController.hpp"

#include "DocumentSession.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace pmxer {
namespace {

mmd::Float3 add(const mmd::Float3 &lhs, const mmd::Float3 &rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

mmd::Float3 subtract(const mmd::Float3 &lhs, const mmd::Float3 &rhs) {
    return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

mmd::Float3 scale(const mmd::Float3 &value, float factor) {
    return {value[0] * factor, value[1] * factor, value[2] * factor};
}

mmd::Float3 transformPoint(const std::array<float, 16> &matrix, const mmd::Float3 &point) {
    return {
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
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

mmd::Float3 rotate(const mmd::Float4 &quaternion, const mmd::Float3 &point) {
    const mmd::Float3 q{quaternion[0], quaternion[1], quaternion[2]};
    const auto cross = [](const mmd::Float3 &lhs, const mmd::Float3 &rhs) {
        return mmd::Float3{lhs[1] * rhs[2] - lhs[2] * rhs[1],
                           lhs[2] * rhs[0] - lhs[0] * rhs[2],
                           lhs[0] * rhs[1] - lhs[1] * rhs[0]};
    };
    return add(point, add(scale(cross(q, point), 2.0F * quaternion[3]),
                          scale(cross(q, cross(q, point)), 2.0F)));
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

void beginDeformGizmoDrag(DocumentSession &session) {
    session.deform.dragVertices.clear();
    session.deform.dragBones.clear();
    if (session.deform.mode == DeformMode::shape) {
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
                session.deform.dragBones.push_back({handle, deformBonePosition(session, handle)});
        }
    }
}

void updateDeformGizmoDrag(DocumentSession &session,
                           const std::array<float, 16> &deltaMatrix) {
    auto &deform = session.deform;
    if (deform.mode == DeformMode::shape) {
        for (const auto &drag : deform.dragVertices) {
            const auto transformed = transformPoint(deltaMatrix, drag.position);
            const auto *base = session.document.resolve(drag.vertex);
            if (base == nullptr)
                continue;
            setVertexDelta(deform, drag.vertex, subtract(transformed, base->position));
        }
        if (deform.symmetryX) {
            const auto &model = session.document.model();
            for (const auto &drag : deform.dragVertices) {
                const auto *base = session.document.resolve(drag.vertex);
                if (base == nullptr)
                    continue;
                const auto reflected = mmd::Float3{2.0F * deform.symmetryCenterX - base->position[0],
                                                  base->position[1], base->position[2]};
                std::size_t nearest{};
                auto distance = std::numeric_limits<float>::max();
                for (std::size_t index = 0; index < model.vertices.size(); ++index) {
                    const auto candidate = subtract(model.vertices[index].position, reflected);
                    const auto candidateDistance = candidate[0] * candidate[0] + candidate[1] * candidate[1] +
                                                   candidate[2] * candidate[2];
                    if (candidateDistance < distance) {
                        distance = candidateDistance;
                        nearest = index;
                    }
                }
                if (distance > std::max(deform.symmetryFeather * deform.symmetryFeather, 1e-8F))
                    continue;
                const auto mirror = session.document.vertexHandle(nearest);
                const auto *mirrorBase = session.document.resolve(mirror);
                if (mirrorBase == nullptr)
                    continue;
                const auto transformed = transformPoint(deltaMatrix, drag.position);
                const auto offset = subtract(transformed, base->position);
                setVertexDelta(deform, mirror, {deform.symmetrySwap ? offset[0] : -offset[0], offset[1], offset[2]});
            }
        }
    } else if (deform.mode == DeformMode::pose) {
        const auto rotation = quaternionFromMatrix(deltaMatrix);
        for (const auto &drag : deform.dragBones) {
            const auto transformed = transformPoint(deltaMatrix, drag.position);
            const auto *base = session.document.resolve(drag.bone);
            if (base != nullptr)
                setBoneDelta(deform, drag.bone, subtract(transformed, base->position), rotation);
        }
    }
    deform.dirty = true;
}

void applyDeformOverlay(const DocumentSession &session, mmd::AnimatedModelFrame &frame) {
    if (session.deform.mode == DeformMode::pose) {
        for (const auto &delta : session.deform.bones) {
            const auto *bone = session.document.resolve(delta.bone);
            if (bone == nullptr)
                continue;
            const auto boneIndex = static_cast<std::size_t>(bone - session.document.model().bones.data());
            for (std::size_t vertexIndex = 0;
                 vertexIndex < frame.vertices.size() && vertexIndex < session.document.model().vertices.size();
                 ++vertexIndex) {
                const auto &source = session.document.model().vertices[vertexIndex];
                const auto weight = [&] {
                    for (std::size_t influence = 0; influence < source.bones.size(); ++influence) {
                        if (source.bones[influence] == static_cast<std::int32_t>(boneIndex)) {
                            if (source.weightType == mmd::PmxWeightType::bdef1)
                                return 1.0F;
                            return source.weights[influence];
                        }
                    }
                    return 0.0F;
                }();
                if (weight <= 0.0F)
                    continue;
                const auto rotated = rotate(delta.rotation, subtract(source.position, bone->position));
                const auto movement = add(subtract(rotated, subtract(source.position, bone->position)),
                                         delta.translation);
                frame.vertices[vertexIndex].position = add(frame.vertices[vertexIndex].position,
                                                           scale(movement, weight));
            }
        }
    }
    if (session.deform.mode == DeformMode::shape) {
        for (const auto &delta : session.deform.vertices) {
            const auto *vertex = session.document.resolve(delta.vertex);
            if (vertex == nullptr)
                continue;
            const auto index = static_cast<std::size_t>(vertex - session.document.model().vertices.data());
            if (index < frame.vertices.size())
                frame.vertices[index].position = add(frame.vertices[index].position, delta.offset);
        }
    }
}

void refreshDeformPreview(DocumentSession &session) {
    if (!session.preview.baseFrame)
        return;
    session.preview.frame = *session.preview.baseFrame;
    applyDeformOverlay(session, *session.preview.frame);
    ++session.preview.frameRevision;
    session.ui.previewFrame = &*session.preview.frame;
}

} // namespace pmxer
