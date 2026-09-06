#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

namespace pmxer {

[[nodiscard]] bool setMaterialTexture(DocumentSession &, mmd::MaterialHandle, std::optional<mmd::TextureHandle>);
[[nodiscard]] bool setMaterialColors(DocumentSession &, mmd::MaterialHandle, mmd::Float4 diffuse, mmd::Float3 ambient);
[[nodiscard]] bool reorderMaterial(DocumentSession &, mmd::MaterialHandle, std::size_t destination);

} // namespace pmxer

