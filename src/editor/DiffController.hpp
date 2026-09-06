#pragma once

#include <mmd/pmx.hpp>

#include "DocumentSession.hpp"

#include <string>
#include <vector>

namespace pmxer {

[[nodiscard]] std::vector<std::string> formatDifferences(const mmd::SemanticCompareResult &result);
[[nodiscard]] mmd::SemanticCompareResult compareWithBaseline(
    const DocumentSession &, mmd::PmxComparisonProfile profile = mmd::PmxComparisonProfile::logical);

} // namespace pmxer
