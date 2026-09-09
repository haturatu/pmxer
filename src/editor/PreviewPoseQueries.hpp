#pragma once

#include <mmd/animation.hpp>
#include <mmd/document.hpp>
#include <mmd/pmx.hpp>

#include <cstddef>

namespace pmxer {

struct DocumentSession;

// Returns the world-space position represented by AnimatedModelFrame::bones.
// The frame stores the global rotation and the translation relative to the
// bind-pose bone position, so this is the single conversion used by viewport
// drawing, picking, and gizmo anchors.
[[nodiscard]] mmd::Float3 evaluatedBonePosition(
    const DocumentSession &session, std::size_t index,
    const mmd::AnimatedModelFrame *frame) noexcept;

[[nodiscard]] mmd::Float3 evaluatedBonePosition(
    const DocumentSession &session, mmd::BoneHandle bone,
    const mmd::AnimatedModelFrame *frame) noexcept;

} // namespace pmxer
