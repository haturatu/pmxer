#pragma once

#include "Selection.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace pmxer {

struct DocumentSession;

[[nodiscard]] std::vector<SelectionItem>
childBones(const DocumentSession &, SelectionItem bone);
[[nodiscard]] std::vector<SelectionItem>
weightedVertices(const DocumentSession &, SelectionItem bone);
[[nodiscard]] std::vector<SelectionItem>
facesForMaterial(const DocumentSession &, SelectionItem material);
[[nodiscard]] std::vector<SelectionItem>
verticesForMaterial(const DocumentSession &, SelectionItem material);
[[nodiscard]] std::vector<SelectionItem>
jointsForRigidBody(const DocumentSession &, SelectionItem rigidBody);
[[nodiscard]] std::vector<SelectionItem>
rigidBodiesForJoint(const DocumentSession &, SelectionItem joint);

struct PhysicsSelectionRelations {
    std::unordered_set<std::uint64_t> selectedBodies;
    std::unordered_set<std::uint64_t> selectedJoints;
    std::unordered_set<std::uint64_t> relatedBodies;
    std::unordered_set<std::uint64_t> relatedJoints;

    [[nodiscard]] bool bodySelected(std::uint64_t id) const noexcept;
    [[nodiscard]] bool jointSelected(std::uint64_t id) const noexcept;
    [[nodiscard]] bool bodyRelated(std::uint64_t id) const noexcept;
    [[nodiscard]] bool jointRelated(std::uint64_t id) const noexcept;
};

[[nodiscard]] PhysicsSelectionRelations
physicsSelectionRelations(const DocumentSession &);

} // namespace pmxer
