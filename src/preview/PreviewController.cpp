#include "PreviewController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace pmxer {
namespace {

void add3(mmd::Float3 &destination, const mmd::Float3 &source, float weight) {
    for (std::size_t index = 0; index < destination.size(); ++index)
        destination[index] += source[index] * weight;
}

void add4(mmd::Float4 &destination, const mmd::Float4 &source, float weight) {
    for (std::size_t index = 0; index < destination.size(); ++index)
        destination[index] += source[index] * weight;
}

mmd::Float4 multiplyQuaternion(const mmd::Float4 &lhs, const mmd::Float4 &rhs) {
    return {
        lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
        lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
        lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2],
    };
}

mmd::Float4 slerpIdentity(mmd::Float4 value, float weight) {
    float length{};
    for (const auto component : value)
        length += component * component;
    if (length <= 1e-12F)
        return {0.0F, 0.0F, 0.0F, 1.0F};
    for (auto &component : value)
        component /= std::sqrt(length);
    if (value[3] < 0.0F)
        for (auto &component : value)
            component = -component;
    const auto angle = std::acos(std::clamp(value[3], -1.0F, 1.0F));
    const auto sine = std::sin(angle);
    if (std::abs(sine) <= 1e-6F)
        return {0.0F, 0.0F, 0.0F, 1.0F};
    const auto first = std::sin((1.0F - weight) * angle) / sine;
    const auto second = std::sin(weight * angle) / sine;
    return {value[0] * second, value[1] * second, value[2] * second,
            first + value[3] * second};
}

} // namespace

PreviewController::PreviewController(const mmd::PmxModel &model)
    : model_(&model), animator_(std::make_unique<mmd::MmdAnimator>(model)) {
#if defined(LIBMMD_HAS_BULLET) || defined(PMXER_ENABLE_PHYSICS)
    physics_ = std::make_unique<mmd::MmdPhysics>(model);
    animator_->setPhysics(physics_.get());
#endif
}

PreviewController::PreviewController(const mmd::PmxDocument &document)
    : document_(&document), model_(&document.model()),
      animator_(std::make_unique<mmd::MmdAnimator>(document.model())) {
#if defined(LIBMMD_HAS_BULLET) || defined(PMXER_ENABLE_PHYSICS)
    physics_ = std::make_unique<mmd::MmdPhysics>(document.model());
    animator_->setPhysics(physics_.get());
#endif
}

PreviewController::~PreviewController() = default;
PreviewController::PreviewController(PreviewController &&) noexcept = default;
PreviewController &PreviewController::operator=(PreviewController &&) noexcept = default;

void PreviewController::setMotion(const mmd::VmdMotion *motion) {
    motion_ = motion;
    rebuildMotion();
}

void PreviewController::setMorphPreview(mmd::MorphHandle morph, float weight) {
    if (document_ == nullptr)
        return;
    const auto *value = document_->resolve(morph);
    if (value == nullptr)
        return;
    const auto index = static_cast<std::size_t>(value - model_->morphs.data());
    const auto found = std::find_if(morphPreviews_.begin(), morphPreviews_.end(),
                                    [&](const auto &preview) { return preview.morph == morph; });
    const auto clamped = std::clamp(weight, 0.0F, 1.0F);
    if (found == morphPreviews_.end())
        morphPreviews_.push_back({morph, index, clamped});
    else {
        found->index = index;
        found->weight = clamped;
    }
}

void PreviewController::setMorphPreview(std::string name, float weight) {
    const auto found = std::find_if(model_->morphs.begin(), model_->morphs.end(),
                                    [&](const auto &morph) { return morph.name == name; });
    if (found == model_->morphs.end())
        return;
    const auto index = static_cast<std::size_t>(found - model_->morphs.begin());
    const auto preview = std::find_if(morphPreviews_.begin(), morphPreviews_.end(),
                                      [&](const auto &value) {
                                          return value.index == index && !value.morph;
                                      });
    const auto clamped = std::clamp(weight, 0.0F, 1.0F);
    if (preview == morphPreviews_.end())
        morphPreviews_.push_back({{}, index, clamped});
    else
        preview->weight = clamped;
}

void PreviewController::clearMorphPreview(mmd::MorphHandle morph) {
    std::erase_if(morphPreviews_, [&](const auto &preview) { return preview.morph == morph; });
}

void PreviewController::clearMorphPreview(const std::string &name) {
    std::erase_if(morphPreviews_, [&](const auto &preview) {
        return preview.index < model_->morphs.size() && model_->morphs[preview.index].name == name;
    });
}

void PreviewController::clearMorphPreviews() {
    morphPreviews_.clear();
}

void PreviewController::setPose(const mmd::VpdPose *pose) {
    animator_->setPose(pose);
}

void PreviewController::setPhysicsEnabled(bool enabled) {
    physicsEnabled_ = enabled;
    animator_->setPhysics(enabled ? physics_.get() : nullptr);
}

void PreviewController::setIkEnabled(bool enabled) {
    ikEnabled_ = enabled;
    animator_->setIkEnabled(enabled);
}

void PreviewController::setFrame(float frame) {
    frame_ = frame < 0.0F ? 0.0F : frame;
}

void PreviewController::reset() {
    frame_ = 0.0F;
#if defined(LIBMMD_HAS_BULLET) || defined(PMXER_ENABLE_PHYSICS)
    if (physics_)
        physics_->reset();
#endif
}

mmd::AnimatedModelFrame PreviewController::evaluate(float deltaSeconds, bool gpuSkinning) {
    auto result = animator_->evaluate(frame_, deltaSeconds, gpuSkinning);
    applyMorphPreviews(result, gpuSkinning);
    frame_ += deltaSeconds * 30.0F;
    return result;
}

const mmd::PmxModel &PreviewController::model() const noexcept {
    return *model_;
}

void PreviewController::rebuildMotion() {
    animator_->setMotion(motion_);
}

void PreviewController::applyMorphPreviews(mmd::AnimatedModelFrame &frame, bool gpuSkinning) const {
    std::vector<std::uint8_t> stack(model_->morphs.size());
    std::function<void(std::size_t, float)> apply = [&](std::size_t index, float weight) {
        if (index >= model_->morphs.size() || stack[index] != 0U || std::abs(weight) <= 1e-7F)
            return;
        stack[index] = 1U;
        const auto &morph = model_->morphs[index];
        if (morph.type == 1U && gpuSkinning && index < frame.morphWeights.size())
            frame.morphWeights[index] += weight;
        for (const auto &offset : morph.offsets) {
            if ((morph.type == 0U || morph.type == 9U) && offset.index >= 0) {
                apply(static_cast<std::size_t>(offset.index), weight * offset.scalar);
            } else if (morph.type == 1U && !gpuSkinning && offset.index >= 0 &&
                       static_cast<std::size_t>(offset.index) < frame.vertices.size()) {
                add3(frame.vertices[static_cast<std::size_t>(offset.index)].position,
                     offset.vector3, weight);
            } else if (morph.type == 2U && offset.index >= 0 &&
                       static_cast<std::size_t>(offset.index) < frame.bones.size()) {
                auto &bone = frame.bones[static_cast<std::size_t>(offset.index)];
                add3(bone.translation, offset.vector3, weight);
                bone.rotation = multiplyQuaternion(bone.rotation,
                                                    slerpIdentity(offset.vector4, weight));
            } else if (morph.type >= 3U && morph.type <= 7U && offset.index >= 0 &&
                       static_cast<std::size_t>(offset.index) < frame.vertices.size()) {
                auto &vertex = frame.vertices[static_cast<std::size_t>(offset.index)];
                if (morph.type == 3U) {
                    for (std::size_t component = 0; component < 2U; ++component)
                        vertex.uv[component] += offset.vector4[component] * weight;
                } else {
                    add4(vertex.additionalUv[morph.type - 4U], offset.vector4, weight);
                }
            } else if (morph.type == 8U) {
                const auto first = offset.index < 0 ? std::size_t{0} : static_cast<std::size_t>(offset.index);
                const auto last = offset.index < 0 ? frame.materials.size()
                                                   : std::min(first + 1U, frame.materials.size());
                for (std::size_t material = first; material < last; ++material) {
                    auto &destination = frame.materials[material];
                    for (std::size_t component = 0; component < 4U; ++component) {
                        const auto value = offset.materialVectors[0][component];
                        if (offset.operation == 0U)
                            destination.diffuse[component] *= 1.0F + (value - 1.0F) * weight;
                        else
                            destination.diffuse[component] += value * weight;
                    }
                }
            }
        }
        stack[index] = 0U;
    };
    for (const auto &preview : morphPreviews_)
        apply(preview.index, preview.weight);
}

} // namespace pmxer
