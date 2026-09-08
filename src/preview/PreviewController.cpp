#include "PreviewController.hpp"

#include <algorithm>
#include <utility>

namespace pmxer {

PreviewController::PreviewController(const mmd::PmxModel &model) : model_(&model), animator_(std::make_unique<mmd::MmdAnimator>(model)) {
#if defined(LIBMMD_HAS_BULLET) || defined(PMXER_ENABLE_PHYSICS)
    physics_ = std::make_unique<mmd::MmdPhysics>(model);
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

void PreviewController::setMorphPreview(std::string name, float weight) {
    morphPreviews_.insert_or_assign(std::move(name), std::clamp(weight, 0.0F, 1.0F));
    rebuildMotion();
}

void PreviewController::clearMorphPreview(const std::string &name) {
    if (morphPreviews_.erase(name) != 0U)
        rebuildMotion();
}

void PreviewController::clearMorphPreviews() {
    if (morphPreviews_.empty())
        return;
    morphPreviews_.clear();
    rebuildMotion();
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
    if (physics_)
        physics_->reset();
}

mmd::AnimatedModelFrame PreviewController::evaluate(float deltaSeconds, bool gpuSkinning) {
    const auto result = animator_->evaluate(frame_, deltaSeconds, gpuSkinning);
    frame_ += deltaSeconds * 30.0F;
    return result;
}

const mmd::PmxModel &PreviewController::model() const noexcept {
    return *model_;
}

void PreviewController::rebuildMotion() {
    if (morphPreviews_.empty()) {
        animator_->setMotion(motion_);
        return;
    }

    previewMotion_ = motion_ == nullptr ? mmd::VmdMotion{} : *motion_;
    std::erase_if(previewMotion_.morphs, [&](const auto &key) { return morphPreviews_.contains(key.name); });
    const auto endFrame = std::max(previewMotion_.lastFrame, std::uint32_t{1});
    for (const auto &[name, weight] : morphPreviews_) {
        previewMotion_.morphs.push_back({name, 0U, weight});
        previewMotion_.morphs.push_back({name, endFrame, weight});
    }
    animator_->setMotion(&previewMotion_);
}

} // namespace pmxer
