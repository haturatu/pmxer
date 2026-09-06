#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

namespace pmxer {

[[nodiscard]] bool setMorphOffset(DocumentSession &, mmd::MorphHandle, std::size_t, mmd::PmxMorphOffset);
[[nodiscard]] bool addVertexMorphOffset(DocumentSession &, mmd::MorphHandle, mmd::VertexHandle, mmd::Float3);
[[nodiscard]] bool addBoneMorphOffset(DocumentSession &, mmd::MorphHandle, mmd::BoneHandle, mmd::Float3, mmd::Float4);
[[nodiscard]] bool addGroupMorphOffset(DocumentSession &, mmd::MorphHandle, mmd::MorphHandle, float);
[[nodiscard]] bool addImpulseMorphOffset(DocumentSession &, mmd::MorphHandle, mmd::RigidBodyHandle, mmd::Float3,
                                          mmd::Float3, bool);

} // namespace pmxer

