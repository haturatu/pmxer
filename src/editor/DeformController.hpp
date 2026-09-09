#pragma once

#include "DeformSession.hpp"

#include <mmd/animation.hpp>

#include <array>
#include <vector>

namespace pmxer {

struct DocumentSession;

[[nodiscard]] mmd::Float3 deformVertexPosition(const DocumentSession &session,
                                                mmd::VertexHandle vertex);
[[nodiscard]] mmd::Float3 deformBonePosition(const DocumentSession &session,
                                              mmd::BoneHandle bone);

void beginDeformGizmoDrag(DocumentSession &session,
                          const std::array<float, 16> &startGizmoMatrix);
void updateDeformGizmoDrag(DocumentSession &session,
                           const std::array<float, 16> &currentGizmoMatrix);
void rebuildSymmetryCache(DocumentSession &session);
[[nodiscard]] std::vector<mmd::PmxMorphOffset>
vertexMorphOffsets(const DocumentSession &session);
[[nodiscard]] std::vector<mmd::PmxMorphOffset>
boneMorphOffsets(const DocumentSession &session);
void refreshDeformPreview(DocumentSession &session);

} // namespace pmxer
