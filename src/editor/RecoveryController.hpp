#pragma once

#include "DocumentSession.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace pmxer {

struct RecoveryResult {
    bool success{};
    std::filesystem::path path;
};

struct RecoveryEntry {
    std::filesystem::path path;
    mmd::PmxModel model;
};

[[nodiscard]] RecoveryResult writeRecovery(const DocumentSession &session);
[[nodiscard]] std::optional<mmd::PmxModel> loadRecovery(const std::filesystem::path &source);
[[nodiscard]] bool discardRecovery(const std::filesystem::path &source);
[[nodiscard]] std::vector<RecoveryEntry> findUntitledRecoveries();
[[nodiscard]] bool discardRecoveryFile(const std::filesystem::path &path);

} // namespace pmxer
