#include "EditorDiagnostics.hpp"

#include <cmath>
#include <filesystem>
#include <numeric>
#include <set>

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
    }
    for (const auto &texture : model.textures) {
        const auto path = std::filesystem::path(texture.storedPath);
        if (path.is_absolute())
            warning(result, "texture", "絶対パスが保存されています");
    }
    for (const auto &joint : model.joints)
        for (std::size_t i = 0; i < 3; ++i)
            if (joint.translationMinimum[i] > joint.translationMaximum[i] ||
                joint.rotationMinimum[i] > joint.rotationMaximum[i])
                warning(result, "joint", "制限の最小値が最大値を超えています");
    for (const auto &body : model.rigidBodies)
        if (body.mass <= 0.0F && body.mode != 0)
            warning(result, "rigidBody", "動的剛体の質量が正ではありません");
    return result;
}

} // namespace pmxer

