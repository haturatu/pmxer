#pragma once

#include <filesystem>

namespace pmxer {

[[nodiscard]] std::filesystem::path temporarySibling(const std::filesystem::path &destination);
[[nodiscard]] bool atomicReplace(const std::filesystem::path &temporary, const std::filesystem::path &destination);

} // namespace pmxer

