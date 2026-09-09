#include "EditorSelectionQueries.hpp"

#include "DocumentSession.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace pmxer {
namespace {

template <typename Handle>
SelectionItem makeItem(SelectionKind kind, Handle handle) {
    return {kind, handle.domain, handle.id, handle.generation};
}

SelectionItem makeReferencedItem(const DocumentSession &session,
                                 SelectionKind kind,
                                 const mmd::ReferenceSite &site) {
    return {kind, session.document.domain(), site.ownerId,
            site.ownerGeneration};
}

} // namespace

std::vector<SelectionItem> childBones(const DocumentSession &session,
                                      SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::BoneTag>(session.document, item);
    for (const auto &site : session.document.referencesTo(target)) {
        if (site.ownerKind == mmd::ReferenceObjectKind::bone &&
            site.field == mmd::ReferenceField::boneParent)
            result.push_back(makeReferencedItem(session, SelectionKind::bone,
                                                site));
    }
    return result;
}

std::vector<SelectionItem> weightedVertices(const DocumentSession &session,
                                            SelectionItem item) {
    std::vector<SelectionItem> result;
    std::unordered_set<std::uint64_t> seen;
    constexpr float kWeightEpsilon = 1.0e-6F;
    const auto target = selectionHandle<mmd::BoneTag>(session.document, item);
    for (const auto &site : session.document.referencesTo(target)) {
        if (site.ownerKind != mmd::ReferenceObjectKind::vertex ||
            site.field != mmd::ReferenceField::vertexBone ||
            site.subIndex >= 4U)
            continue;
        const mmd::VertexHandle vertex{session.document.domain(), site.ownerId,
                                       site.ownerGeneration};
        const auto *value = session.document.resolve(vertex);
        if (value == nullptr || value->weights[site.subIndex] <= kWeightEpsilon)
            continue;
        if (seen.insert(vertex.id).second)
            result.push_back(makeItem(SelectionKind::vertex, vertex));
    }
    return result;
}

std::vector<SelectionItem> facesForMaterial(const DocumentSession &session,
                                            SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::MaterialTag>(session.document, item);
    for (const auto &site : session.document.referencesTo(target)) {
        if (site.ownerKind == mmd::ReferenceObjectKind::face &&
            site.field == mmd::ReferenceField::faceMaterial)
            result.push_back(makeReferencedItem(session, SelectionKind::face,
                                                site));
    }
    return result;
}

std::vector<SelectionItem> verticesForMaterial(const DocumentSession &session,
                                               SelectionItem item) {
    std::vector<SelectionItem> result;
    std::unordered_set<std::uint64_t> seen;
    for (const auto &face : facesForMaterial(session, item)) {
        const auto faceHandle = selectionHandle<mmd::FaceTag>(session.document,
                                                               face);
        const auto *value = session.document.resolve(faceHandle);
        if (value == nullptr)
            continue;
        for (const auto vertex : value->vertices) {
            if (seen.insert(vertex.id).second)
                result.push_back(makeItem(SelectionKind::vertex, vertex));
        }
    }
    return result;
}

std::vector<SelectionItem> jointsForRigidBody(const DocumentSession &session,
                                              SelectionItem item) {
    std::vector<SelectionItem> result;
    std::unordered_set<std::uint64_t> seen;
    const auto target = selectionHandle<mmd::RigidBodyTag>(session.document,
                                                            item);
    for (const auto &site : session.document.referencesTo(target)) {
        if (site.ownerKind != mmd::ReferenceObjectKind::joint ||
            (site.field != mmd::ReferenceField::jointBodyA &&
             site.field != mmd::ReferenceField::jointBodyB) ||
            !seen.insert(site.ownerId).second)
            continue;
        result.push_back(makeReferencedItem(session, SelectionKind::joint,
                                            site));
    }
    return result;
}

std::vector<SelectionItem> rigidBodiesForJoint(const DocumentSession &session,
                                               SelectionItem item) {
    std::vector<SelectionItem> result;
    const auto target = selectionHandle<mmd::JointTag>(session.document, item);
    const auto *joint = session.document.resolve(target);
    if (joint == nullptr)
        return result;
    const auto appendBody = [&](std::int32_t index) {
        if (index < 0 || static_cast<std::size_t>(index) >=
                             session.document.model().rigidBodies.size())
            return;
        const auto body = session.document.rigidBodyHandle(
            static_cast<std::size_t>(index));
        const auto bodyItem = makeItem(SelectionKind::rigidBody, body);
        if (std::find(result.begin(), result.end(), bodyItem) == result.end())
            result.push_back(bodyItem);
    };
    appendBody(joint->bodyA);
    appendBody(joint->bodyB);
    return result;
}

bool PhysicsSelectionRelations::bodySelected(std::uint64_t id) const noexcept {
    return selectedBodies.contains(id);
}

bool PhysicsSelectionRelations::jointSelected(std::uint64_t id) const noexcept {
    return selectedJoints.contains(id);
}

bool PhysicsSelectionRelations::bodyRelated(std::uint64_t id) const noexcept {
    return relatedBodies.contains(id);
}

bool PhysicsSelectionRelations::jointRelated(std::uint64_t id) const noexcept {
    return relatedJoints.contains(id);
}

PhysicsSelectionRelations
physicsSelectionRelations(const DocumentSession &session) {
    PhysicsSelectionRelations result;
    std::unordered_set<std::uint64_t> selectedBodies;
    std::unordered_set<std::uint64_t> selectedJoints;
    std::unordered_set<std::uint64_t> relatedBodies;
    std::unordered_set<std::uint64_t> relatedJoints;
    for (const auto &item : session.selection.items()) {
        if (item.kind == SelectionKind::rigidBody) {
            selectedBodies.insert(item.id);
            const mmd::RigidBodyHandle body{item.domain, item.id,
                                            item.generation};
            for (const auto &site : session.document.referencesTo(body)) {
                if (site.ownerKind == mmd::ReferenceObjectKind::joint &&
                    (site.field == mmd::ReferenceField::jointBodyA ||
                     site.field == mmd::ReferenceField::jointBodyB))
                    relatedJoints.insert(site.ownerId);
            }
        } else if (item.kind == SelectionKind::joint) {
            selectedJoints.insert(item.id);
            const mmd::JointHandle joint{item.domain, item.id,
                                         item.generation};
            const auto *value = session.document.resolve(joint);
            if (value == nullptr)
                continue;
            const auto addBody = [&](std::int32_t index) {
                if (index >= 0 && static_cast<std::size_t>(index) <
                                      session.document.model().rigidBodies.size())
                    relatedBodies.insert(session.document
                                             .rigidBodyHandle(static_cast<std::size_t>(index))
                                             .id);
            };
            addBody(value->bodyA);
            addBody(value->bodyB);
        }
    }
    result.selectedBodies = std::move(selectedBodies);
    result.selectedJoints = std::move(selectedJoints);
    result.relatedBodies = std::move(relatedBodies);
    result.relatedJoints = std::move(relatedJoints);
    return result;
}

} // namespace pmxer
