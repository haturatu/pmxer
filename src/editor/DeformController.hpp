#pragma once

#include "DeformSession.hpp"

#include <mmd/animation.hpp>

#include <array>

namespace pmxer {

struct DocumentSession;

[[nodiscard]] mmd::Float3 deformVertexPosition(const DocumentSession &session,
                                                mmd::VertexHandle vertex);
[[nodiscard]] mmd::Float3 deformBonePosition(const DocumentSession &session,
                                              mmd::BoneHandle bone);

void beginDeformGizmoDrag(DocumentSession &session);
void updateDeformGizmoDrag(DocumentSession &session,
                           const std::array<float, 16> &deltaMatrix);
void refreshDeformPreview(DocumentSession &session);
void applyDeformOverlay(const DocumentSession &session,
                        mmd::AnimatedModelFrame &frame);

} // namespace pmxer
