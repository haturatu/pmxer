#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

namespace pmxer {

[[nodiscard]] bool addBoneToDisplayFrame(DocumentSession &, mmd::DisplayFrameHandle, mmd::BoneHandle);
[[nodiscard]] bool addMorphToDisplayFrame(DocumentSession &, mmd::DisplayFrameHandle, mmd::MorphHandle);
[[nodiscard]] bool moveDisplayItem(DocumentSession &, mmd::DisplayFrameHandle, std::size_t from, std::size_t to);

} // namespace pmxer

