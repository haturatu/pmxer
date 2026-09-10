#pragma once

#include "DeformSession.hpp"
#include "WorkspacePolicy.hpp"

#include <mmd/animation.hpp>

#include <array>
#include <vector>

namespace pmxer {

struct DocumentSession;

[[nodiscard]] mmd::Float3 deformVertexPosition(const DocumentSession &session,
                                                mmd::VertexHandle vertex);
void discardPendingTransformEdit(DocumentSession &session);
void discardPendingVertexEdit(DocumentSession &session);
void discardPendingBoneEdit(DocumentSession &session);
void discardAllPendingTransformEdits(DocumentSession &session);
void activateTransformTab(DocumentSession &session, EditorWorkspace &workspace,
                          TransformViewTab tab);

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
