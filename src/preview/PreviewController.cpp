#include "PreviewController.hpp"

#include <algorithm>
#include <utility>

namespace pmxer {

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
    rebuildMorphOverrides();
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
    rebuildMorphOverrides();
}

void PreviewController::setBonePreview(std::vector<mmd::PmxMorphOffset> offsets) {
    bonePreviewOffsets_ = std::move(offsets);
    rebuildMorphOverrides();
}

void PreviewController::setVertexPreview(std::vector<mmd::PmxMorphOffset> offsets) {
    vertexPreviewOffsets_ = std::move(offsets);
    rebuildMorphOverrides();
}

void PreviewController::clearMorphPreview(mmd::MorphHandle morph) {
    std::erase_if(morphPreviews_, [&](const auto &preview) { return preview.morph == morph; });
    rebuildMorphOverrides();
}

void PreviewController::clearMorphPreview(const std::string &name) {
    std::erase_if(morphPreviews_, [&](const auto &preview) {
        return preview.index < model_->morphs.size() && model_->morphs[preview.index].name == name;
    });
    rebuildMorphOverrides();
}

void PreviewController::clearMorphPreviews() {
    morphPreviews_.clear();
    rebuildMorphOverrides();
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
    auto result = animator_->evaluate(frame_, deltaSeconds, gpuSkinning, morphOverrides_);
    frame_ += deltaSeconds * 30.0F;
    return result;
}

const mmd::PmxModel &PreviewController::model() const noexcept {
    return *model_;
}

void PreviewController::rebuildMotion() {
    animator_->setMotion(motion_);
}

void PreviewController::rebuildMorphOverrides() {
    morphOverrides_.clear();
    morphOverrides_.reserve(morphPreviews_.size() + (vertexPreviewOffsets_.empty() ? 0U : 1U) +
                            (bonePreviewOffsets_.empty() ? 0U : 1U));
    for (const auto &preview : morphPreviews_)
        morphOverrides_.push_back({preview.index, preview.weight});
    if (!vertexPreviewOffsets_.empty())
        morphOverrides_.push_back({mmd::MorphOverride::temporary, 1.0F, 1U, vertexPreviewOffsets_});
    if (!bonePreviewOffsets_.empty())
        morphOverrides_.push_back({mmd::MorphOverride::temporary, 1.0F, 2U, bonePreviewOffsets_});
}

} // namespace pmxer
