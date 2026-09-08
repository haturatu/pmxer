#pragma once

#include "Selection.hpp"

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

} // namespace pmxer
