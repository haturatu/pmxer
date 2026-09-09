#pragma once

#include <mmd/animation.hpp>
#include <mmd/document.hpp>
#include <mmd/pmx.hpp>

#include <cstddef>

namespace pmxer {

struct DocumentSession;

// Returns the world-space position and rotation represented by
// AnimatedModelFrame::bones. These are the single evaluated-pose queries used
// by viewport drawing, picking, and gizmo anchors.
[[nodiscard]] mmd::Float3 evaluatedBonePosition(
    const DocumentSession &session, std::size_t index,
    const mmd::AnimatedModelFrame *frame) noexcept;

[[nodiscard]] mmd::Float3 evaluatedBonePosition(
    const DocumentSession &session, mmd::BoneHandle bone,
    const mmd::AnimatedModelFrame *frame) noexcept;

[[nodiscard]] mmd::Float4 evaluatedBoneRotation(
    const DocumentSession &session, std::size_t index,
    const mmd::AnimatedModelFrame *frame) noexcept;

[[nodiscard]] mmd::Float4 evaluatedBoneRotation(
    const DocumentSession &session, mmd::BoneHandle bone,
    const mmd::AnimatedModelFrame *frame) noexcept;

} // namespace pmxer
