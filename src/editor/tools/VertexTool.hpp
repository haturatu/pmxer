#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

namespace pmxer {

[[nodiscard]] bool setVertexPosition(DocumentSession &, mmd::VertexHandle, mmd::Float3);
[[nodiscard]] bool setVertexNormal(DocumentSession &, mmd::VertexHandle, mmd::Float3);
[[nodiscard]] bool setVertexUv(DocumentSession &, mmd::VertexHandle, mmd::Float2);
[[nodiscard]] bool setVertexWeightType(DocumentSession &, mmd::VertexHandle, mmd::PmxWeightType);

} // namespace pmxer

