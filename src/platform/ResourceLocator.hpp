#pragma once

#include <filesystem>

namespace pmxer {

[[nodiscard]] std::filesystem::path resolveResourceDirectory(
    const std::filesystem::path &requested,
    const std::filesystem::path &executableDirectory);

[[nodiscard]] std::filesystem::path resolveUiFont(
    const std::filesystem::path &requested,
    const std::filesystem::path &resourceDirectory);

} // namespace pmxer
