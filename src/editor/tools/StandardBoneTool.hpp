#pragma once

#include "../DocumentSession.hpp"

#include <string>
#include <vector>

namespace pmxer {

struct BoneRecipe {
    std::string name;
    std::string parent;
    std::int32_t deformLayer{};
};

struct RecipeCheck {
    std::size_t available{};
    std::size_t missing{};
};

[[nodiscard]] const std::vector<BoneRecipe> &standardBoneRecipes();
[[nodiscard]] RecipeCheck inspectStandardBones(const mmd::PmxModel &model);
[[nodiscard]] bool applyStandardBones(DocumentSession &, const std::vector<BoneRecipe> &recipes);

} // namespace pmxer

