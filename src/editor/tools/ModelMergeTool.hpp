#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <string>
#include <vector>

namespace pmxer {

struct MergeConflict {
    std::string kind;
    std::string name;
    std::string leftName;
    std::string rightName;
};

struct MergeReport {
    std::vector<MergeConflict> conflicts;
    std::size_t addedVertices{};
    std::size_t addedMaterials{};
    std::size_t addedBones{};
    std::size_t addedMorphs{};
};

[[nodiscard]] MergeReport inspectMerge(const mmd::PmxModel &, const mmd::PmxModel &);
[[nodiscard]] bool mergeAppend(DocumentSession &, const mmd::PmxModel &, MergeReport *report = nullptr);

} // namespace pmxer

