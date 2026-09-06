#include "StandardBoneTool.hpp"

#include <algorithm>

namespace pmxer {
namespace {

const std::vector<BoneRecipe> recipes{
    {"root", "", 0}, {"groove", "root", 0}, {"waist", "root", 0}, {"upper", "waist", 0},
    {"upper2", "upper", 0}, {"arm_twist_l", "upper", 0}, {"arm_twist_r", "upper", 0},
    {"wrist_twist_l", "arm_twist_l", 0}, {"wrist_twist_r", "arm_twist_r", 0},
    {"leg_d_l", "root", 0}, {"knee_d_l", "leg_d_l", 0}, {"ankle_d_l", "knee_d_l", 0},
    {"leg_d_r", "root", 0}, {"knee_d_r", "leg_d_r", 0}, {"ankle_d_r", "knee_d_r", 0},
    {"toe_ex_l", "ankle_d_l", 0}, {"toe_ex_r", "ankle_d_r", 0},
};

std::optional<mmd::BoneHandle> findBone(const mmd::PmxDocument &document, const std::string &name) {
    for (std::size_t i = 0; i < document.model().bones.size(); ++i)
        if (document.model().bones[i].name == name)
            return document.boneHandle(i);
    return std::nullopt;
}

} // namespace

const std::vector<BoneRecipe> &standardBoneRecipes() {
    return recipes;
}

RecipeCheck inspectStandardBones(const mmd::PmxModel &model) {
    RecipeCheck result;
    for (const auto &recipe : recipes) {
        const auto found = std::find_if(model.bones.begin(), model.bones.end(), [&](const auto &bone) {
            return bone.name == recipe.name;
        });
        if (found == model.bones.end())
            ++result.missing;
        else
            ++result.available;
    }
    return result;
}

bool applyStandardBones(DocumentSession &session, const std::vector<BoneRecipe> &requested) {
    auto transaction = session.document.transaction();
    for (const auto &recipe : requested) {
        if (findBone(session.document, recipe.name))
            continue;
        mmd::PmxBone bone;
        bone.name = recipe.name;
        bone.englishName = recipe.name;
        bone.deformLayer = recipe.deformLayer;
        mmd::BoneDraft draft{bone, findBone(session.document, recipe.parent)};
        if (!transaction.addBone(std::move(draft)))
            return false;
    }
    const auto result = transaction.commit();
    if (!result.committed)
        return false;
    session.modified = true;
    session.validation = result.validation;
    return true;
}

} // namespace pmxer

