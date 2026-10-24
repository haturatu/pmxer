#pragma once

#include <filesystem>

namespace pmxer {

// Prefer the exact file. On case-sensitive filesystems, accept a unique ASCII
// case-insensitive match per path component for Windows-authored model assets.
// Missing or ambiguous paths are returned unchanged; no files are modified.
[[nodiscard]] std::filesystem::path resolveFileCase(const std::filesystem::path &path);

[[nodiscard]] std::filesystem::path resolveResourceDirectory(
    const std::filesystem::path &requested,
    const std::filesystem::path &executableDirectory);

[[nodiscard]] std::filesystem::path resolveUiFont(
    const std::filesystem::path &requested,
    const std::filesystem::path &resourceDirectory,
    bool simplifiedChinese = false);

} // namespace pmxer
