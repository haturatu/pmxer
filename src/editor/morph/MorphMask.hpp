#pragma once

#include "MorphOps.hpp"

#include <mmd/pmx.hpp>

#include <cstddef>
#include <optional>

namespace pmxer::morph {

enum class MaterialFilterMode : std::uint8_t {
    excludeUsed,
    excludeExclusive,
    keepOnlyUsed,
    keepOnlyExclusive,
};

[[nodiscard]] MorphData filterByMaterial(const mmd::PmxModel &model,
                                          const mmd::PmxMorph &morph,
                                          std::optional<std::size_t> material,
                                          MaterialFilterMode mode);

} // namespace pmxer::morph
