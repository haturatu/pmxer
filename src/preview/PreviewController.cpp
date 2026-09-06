#include "PreviewController.hpp"

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
    animator_->setMotion(motion);
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
    (void)ikEnabled_;
}

void PreviewController::setFrame(float frame) {
    frame_ = frame < 0.0F ? 0.0F : frame;
}

void PreviewController::reset() {
    frame_ = 0.0F;
    if (physics_)
        physics_->reset();
}

mmd::AnimatedModelFrame PreviewController::evaluate(float deltaSeconds) {
    if (physicsEnabled_ && physics_ && deltaSeconds > 0.0F)
        physics_->step(deltaSeconds);
    const auto result = animator_->evaluate(frame_, deltaSeconds);
    frame_ += deltaSeconds * 30.0F;
    return result;
}

const mmd::PmxModel &PreviewController::model() const noexcept {
    return *model_;
}

} // namespace pmxer

