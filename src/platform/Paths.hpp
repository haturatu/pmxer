#pragma once

#include <filesystem>

namespace pmxer {

[[nodiscard]] std::filesystem::path recoveryDirectory();
[[nodiscard]] std::filesystem::path recoveryPath(const std::filesystem::path &source);

} // namespace pmxer

