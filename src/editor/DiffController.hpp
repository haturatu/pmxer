#pragma once

#include <mmd/pmx.hpp>

#include <string>
#include <vector>

namespace pmxer {

[[nodiscard]] std::vector<std::string> formatDifferences(const mmd::SemanticCompareResult &result);

} // namespace pmxer

