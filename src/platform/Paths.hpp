#pragma once

#include <filesystem>
#include <string_view>

namespace pmxer {

[[nodiscard]] std::filesystem::path recoveryDirectory();
[[nodiscard]] std::filesystem::path recoveryPath(const std::filesystem::path &source);
[[nodiscard]] std::filesystem::path recoveryPath(const std::filesystem::path &source, std::string_view recoveryId);

} // namespace pmxer
