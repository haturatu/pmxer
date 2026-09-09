#pragma once

#include <mmd/pmx.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pmxer::morph {

struct MorphData {
    std::uint8_t type{};
    std::vector<mmd::PmxMorphOffset> offsets;
    std::uint8_t panel{4U};
    std::string englishName;
};

struct SideSplitOptions {
    float centerX{};
    float feather{0.02F};
    bool swapSides{};
    bool duplicateCenterVertices{};
};

struct SideSplitResult {
    MorphData left;
    MorphData right;
};

struct InvertResult {
    bool success{};
    MorphData data;
    std::string message;
};

[[nodiscard]] MorphData copy(const mmd::PmxMorph &morph);
[[nodiscard]] MorphData scale(MorphData value, float factor);
[[nodiscard]] InvertResult invert(MorphData value);
[[nodiscard]] MorphData pruneZeroOffsets(MorphData value);
[[nodiscard]] MorphData duplicate(const MorphData &value);
[[nodiscard]] MorphData combine(std::span<const MorphData> values);
[[nodiscard]] MorphData subtract(const MorphData &lhs, const MorphData &rhs);
[[nodiscard]] SideSplitResult splitSide(const mmd::PmxModel &model,
                                         const mmd::PmxMorph &morph,
                                         SideSplitOptions options = {});

} // namespace pmxer::morph
