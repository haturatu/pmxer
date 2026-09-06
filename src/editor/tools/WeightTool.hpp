#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <vector>

namespace pmxer {

[[nodiscard]] bool assignBone(DocumentSession &, const std::vector<mmd::VertexHandle> &, mmd::BoneHandle,
                               std::size_t slot);
[[nodiscard]] bool mirrorWeights(DocumentSession &, const std::vector<mmd::VertexHandle> &,
                                  const std::vector<mmd::VertexHandle> &);
[[nodiscard]] bool pruneWeights(DocumentSession &, float threshold);

} // namespace pmxer

