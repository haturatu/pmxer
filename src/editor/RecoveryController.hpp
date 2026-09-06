#pragma once

#include "DocumentSession.hpp"

#include <filesystem>
#include <optional>

namespace pmxer {

struct RecoveryResult {
    bool success{};
    std::filesystem::path path;
};

[[nodiscard]] RecoveryResult writeRecovery(const DocumentSession &session);
[[nodiscard]] std::optional<mmd::PmxModel> loadRecovery(const std::filesystem::path &source);
[[nodiscard]] bool discardRecovery(const std::filesystem::path &source);

} // namespace pmxer

