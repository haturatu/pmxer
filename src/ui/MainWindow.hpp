#pragma once

#include <filesystem>

namespace pmxer {

[[nodiscard]] int runApplication(const std::filesystem::path *initialPath = nullptr);

} // namespace pmxer

