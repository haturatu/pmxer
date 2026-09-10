#pragma once

#include <mmd/pmx.hpp>

#include <array>

namespace pmxer {

[[nodiscard]] mmd::Float4 quaternionFromEuler(const mmd::Float3 &euler) noexcept;
[[nodiscard]] std::array<float, 16>
composeRotationMatrix(const mmd::Float4 &rotation) noexcept;

} // namespace pmxer
