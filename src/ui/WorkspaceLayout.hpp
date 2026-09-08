#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace pmxer {

inline constexpr std::uint32_t kWorkspaceLayoutVersion = 2;

enum class WorkspaceLayoutLoadResult {
    missing,
    loaded,
    legacy,
    unsupportedVersion,
    corrupt,
};

struct WorkspaceLayout {
    std::uint32_t version{};
    std::string imguiIni;
};

[[nodiscard]] WorkspaceLayoutLoadResult loadWorkspaceLayout(
    const std::filesystem::path &path, WorkspaceLayout &layout);
[[nodiscard]] bool saveWorkspaceLayout(const std::filesystem::path &path,
                                        std::string_view imguiIni);
[[nodiscard]] bool backupWorkspaceLayout(const std::filesystem::path &path);

} // namespace pmxer
