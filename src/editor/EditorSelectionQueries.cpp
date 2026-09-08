#include "EditorSelectionQueries.hpp"

#include "DocumentSession.hpp"

#include <algorithm>
#include <optional>

namespace pmxer {
namespace {

template <typename Handle>
SelectionItem makeItem(SelectionKind kind, Handle handle) {
    return {kind, handle.domain, handle.id, handle.generation};
}

template <typename Handle>
void appendUnique(std::vector<SelectionItem> &items, SelectionKind kind,
                  Handle handle) {
    const auto item = makeItem(kind, handle);
    if (std::find(items.begin(), items.end(), item) == items.end())
        items.push_back(item);
}

} // namespace

std::vector<SelectionItem> childBones(const DocumentSession &session,
                                      SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::BoneTag>(session.document, item);
    const auto &model = session.document.model();
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < model.bones.size(); ++i)
        if (session.document.boneHandle(i) == target)
            index = i;
    if (!index)
        return result;
    for (std::size_t i = 0; i < model.bones.size(); ++i)
        if (model.bones[i].parent == static_cast<std::int32_t>(*index))
            appendUnique(result, SelectionKind::bone,
                         session.document.boneHandle(i));
    return result;
}

std::vector<SelectionItem> weightedVertices(const DocumentSession &session,
                                            SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::BoneTag>(session.document, item);
    const auto &model = session.document.model();
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < model.bones.size(); ++i)
        if (session.document.boneHandle(i) == target)
            index = i;
    if (!index)
        return result;
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        const auto &vertex = model.vertices[i];
        const auto count = vertex.weightType == mmd::PmxWeightType::bdef1
                               ? 1U
                           : vertex.weightType == mmd::PmxWeightType::bdef2 ||
                                   vertex.weightType == mmd::PmxWeightType::sdef
                               ? 2U
                               : 4U;
        for (std::size_t slot = 0; slot < count; ++slot) {
            if (vertex.bones[slot] == static_cast<std::int32_t>(*index)) {
                appendUnique(result, SelectionKind::vertex,
                             session.document.vertexHandle(i));
                break;
            }
        }
    }
    return result;
}

std::vector<SelectionItem> facesForMaterial(const DocumentSession &session,
                                            SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::MaterialTag>(session.document, item);
    const auto &model = session.document.model();
    std::size_t offset{};
    for (std::size_t material = 0; material < model.materials.size(); ++material) {
        const auto handle = session.document.materialHandle(material);
        if (handle == target) {
            const auto count = model.materials[material].indexCount / 3U;
            for (std::size_t face = offset / 3U; face < offset / 3U + count; ++face)
                appendUnique(result, SelectionKind::face,
                             session.document.faceHandle(face));
            break;
        }
        offset += model.materials[material].indexCount;
    }
    return result;
}

std::vector<SelectionItem> verticesForMaterial(const DocumentSession &session,
                                               SelectionItem item) {
    std::vector<SelectionItem> result;
    for (const auto &face : facesForMaterial(session, item)) {
        const auto faceHandle = selectionHandle<mmd::FaceTag>(session.document, face);
        for (std::size_t index = 0; index < session.document.faces().size(); ++index) {
            if (session.document.faceHandle(index) != faceHandle)
                continue;
            const auto &value = session.document.faces()[index];
            for (const auto vertex : value.vertices)
                appendUnique(result, SelectionKind::vertex, vertex);
            break;
        }
    }
    return result;
}

std::vector<SelectionItem> jointsForRigidBody(const DocumentSession &session,
                                              SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::RigidBodyTag>(session.document, item);
    const auto &model = session.document.model();
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < model.rigidBodies.size(); ++i)
        if (session.document.rigidBodyHandle(i) == target)
            index = i;
    if (!index)
        return result;
    for (std::size_t i = 0; i < model.joints.size(); ++i)
        if (model.joints[i].bodyA == static_cast<std::int32_t>(*index) ||
            model.joints[i].bodyB == static_cast<std::int32_t>(*index))
            appendUnique(result, SelectionKind::joint,
                         session.document.jointHandle(i));
    return result;
}

std::vector<SelectionItem> rigidBodiesForJoint(const DocumentSession &session,
                                               SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::JointTag>(session.document, item);
    const auto &model = session.document.model();
    for (std::size_t i = 0; i < model.joints.size(); ++i) {
        if (session.document.jointHandle(i) != target)
            continue;
        for (const auto body : {model.joints[i].bodyA, model.joints[i].bodyB})
            if (body >= 0 && static_cast<std::size_t>(body) < model.rigidBodies.size())
                appendUnique(result, SelectionKind::rigidBody,
                             session.document.rigidBodyHandle(static_cast<std::size_t>(body)));
        break;
    }
    return result;
}

} // namespace pmxer
