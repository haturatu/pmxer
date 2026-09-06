#include "DiffController.hpp"

#include <string>

namespace pmxer {

std::vector<std::string> formatDifferences(const mmd::SemanticCompareResult &result) {
    std::vector<std::string> lines;
    lines.reserve(result.differences.size());
    for (const auto &difference : result.differences)
        lines.push_back(difference.path + ": " + difference.message);
    return lines;
}

} // namespace pmxer

