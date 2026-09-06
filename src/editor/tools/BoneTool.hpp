#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

namespace pmxer {

[[nodiscard]] bool setBoneParent(DocumentSession &, mmd::BoneHandle, mmd::BoneHandle);
[[nodiscard]] bool setBoneIkTarget(DocumentSession &, mmd::BoneHandle, mmd::BoneHandle);
[[nodiscard]] bool reorderBone(DocumentSession &, mmd::BoneHandle, std::size_t destination);
[[nodiscard]] bool addIkLink(DocumentSession &, mmd::BoneHandle, mmd::PmxIkLink);

} // namespace pmxer

