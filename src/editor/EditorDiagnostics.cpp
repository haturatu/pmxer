#include "EditorDiagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <set>
#include <unordered_set>

namespace pmxer {
namespace {

void warning(mmd::ValidationResult &result, std::string object, std::string message) {
    result.issues.push_back({mmd::ValidationSeverity::warning, mmd::ValidationCode::generic, std::move(object),
                             std::move(message), {}});
}

template <typename Value>
void checkNames(mmd::ValidationResult &result, const std::vector<Value> &values, const char *label) {
    std::set<std::string> names;
    for (const auto &value : values) {
        if (value.name.empty())
            warning(result, label, "名前が空です");
        if (!names.insert(value.name).second)
            warning(result, label, "名前が重複しています");
    }
}

} // namespace

mmd::ValidationResult validateForEditing(const mmd::PmxModel &model) {
    auto result = mmd::pmx::validate(model);
    checkNames(result, model.materials, "material");
    checkNames(result, model.bones, "bone");
    checkNames(result, model.morphs, "morph");
    for (const auto &material : model.materials)
        if (material.indexCount == 0)
            warning(result, "material", "面を持たない材質です");
    for (const auto &vertex : model.vertices) {
        const auto count = vertex.weightType == mmd::PmxWeightType::bdef1
                               ? std::size_t{1}
                               : (vertex.weightType == mmd::PmxWeightType::bdef2 ||
                                          vertex.weightType == mmd::PmxWeightType::sdef
                                      ? std::size_t{2}
                                      : std::size_t{4});
        const auto sum = std::accumulate(vertex.weights.begin(), vertex.weights.begin() + static_cast<std::ptrdiff_t>(count), 0.0F);
        if (!std::isfinite(sum) || sum < 0.999F || sum > 1.001F)
            warning(result, "vertex", "ウェイト合計が1ではありません");
        for (std::size_t i = 0; i < count; ++i)
            if (vertex.weights[i] < 0.0F)
                warning(result, "vertex", "負のウェイトがあります");
        if (vertex.weightType == mmd::PmxWeightType::sdef) {
            if (vertex.bones[0] == vertex.bones[1])
                warning(result, "vertex", "SDEFのボーンが重複しています");
            const auto finite = [](const mmd::Float3 &value) {
                return std::all_of(value.begin(), value.end(), [](float item) { return std::isfinite(item); });
            };
            if (!finite(vertex.sdefC) || !finite(vertex.sdefR0) || !finite(vertex.sdefR1))
                warning(result, "vertex", "SDEFパラメータが有限値ではありません");
        }
    }
    std::set<std::string> lowerTextureNames;
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        const auto &texture = model.textures[i];
        const auto path = std::filesystem::path(texture.storedPath);
        if (path.is_absolute())
            warning(result, "texture", "絶対パスが保存されています");
        if (!texture.storedPath.empty() && !std::filesystem::exists(mmd::pmx::resolveTexturePath(model, i)))
            warning(result, "texture", "テクスチャが見つかりません");
        auto folded = texture.storedPath;
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (!folded.empty() && !lowerTextureNames.insert(std::move(folded)).second)
            warning(result, "texture", "大文字小文字だけが異なるテクスチャパスがあります");
    }
    for (const auto &morph : model.morphs)
        if (morph.offsets.empty())
            warning(result, "morph", "オフセットを持たないモーフです");
    for (const auto &frame : model.displayFrames) {
        std::set<std::pair<bool, std::int32_t>> items;
        for (const auto &item : frame.items)
            if (!items.insert({item.bone, item.index}).second)
                warning(result, "displayFrame", "表示枠項目が重複しています");
    }
    for (const auto &bone : model.bones) {
        std::set<std::int32_t> links;
        for (const auto &link : bone.ikLinks) {
            if (!links.insert(link.bone).second)
                warning(result, "bone", "IKリンクが重複しています");
            if (link.bone == bone.ikTarget)
                warning(result, "bone", "IK対象自身がリンクされています");
        }
        if (bone.ikTarget == static_cast<std::int32_t>(&bone - model.bones.data()))
            warning(result, "bone", "IK対象が自身です");
        if (bone.inheritParent >= 0 && bone.inheritParent < static_cast<std::int32_t>(model.bones.size()) &&
            model.bones[static_cast<std::size_t>(bone.inheritParent)].deformLayer > bone.deformLayer)
            warning(result, "bone", "付与元の変形順が後です");
    }
    for (const auto &joint : model.joints) {
        for (std::size_t i = 0; i < 3; ++i)
            if (joint.translationMinimum[i] > joint.translationMaximum[i] ||
                joint.rotationMinimum[i] > joint.rotationMaximum[i])
                warning(result, "joint", "制限の最小値が最大値を超えています");
        if (joint.type != 0)
            warning(result, "joint", "このジョイント種別は保存のみ対応です");
    }
    for (const auto &body : model.rigidBodies)
        if (body.mass <= 0.0F && body.mode != 0)
            warning(result, "rigidBody", "動的剛体の質量が正ではありません");
        else if (std::any_of(body.size.begin(), body.size.end(), [](float value) { return value <= 0.0F; }))
            warning(result, "rigidBody", "剛体の大きさが正ではありません");
    if (!model.softBodies.empty() && model.metadata.version < 2.1F)
        warning(result, "softBody", "ソフトボディはPMX 2.1でのみ利用できます");
    if (!model.softBodies.empty())
        warning(result, "softBody", "ソフトボディの完全な物理プレビューは未対応です");
    return result;
}

} // namespace pmxer
