#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <filesystem>
#include <vector>

namespace pmxer {

[[nodiscard]] bool relinkTexture(DocumentSession &, mmd::TextureHandle, std::string storedPath);
[[nodiscard]] std::vector<std::size_t> missingTextures(const mmd::PmxModel &);
[[nodiscard]] bool convertAbsoluteTextures(DocumentSession &, const std::filesystem::path &root);

} // namespace pmxer

