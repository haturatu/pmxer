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

mmd::SemanticCompareResult compareWithBaseline(const DocumentSession &session, mmd::PmxComparisonProfile profile) {
    if (!session.baseline) {
        mmd::SemanticCompareResult result;
        result.differences.push_back({"baseline", "baseline is unavailable"});
        return result;
    }
    return mmd::pmx::semanticCompare(*session.baseline, session.document.model(), profile);
}

} // namespace pmxer
