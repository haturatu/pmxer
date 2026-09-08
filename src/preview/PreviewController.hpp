#pragma once

#include <mmd/animation.hpp>
#include <mmd/physics.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace pmxer {

class PreviewController {
  public:
    explicit PreviewController(const mmd::PmxModel &model);
    ~PreviewController();
    PreviewController(PreviewController &&) noexcept;
    PreviewController &operator=(PreviewController &&) noexcept;
    PreviewController(const PreviewController &) = delete;
    PreviewController &operator=(const PreviewController &) = delete;

    void setMotion(const mmd::VmdMotion *motion);
    void setMorphPreview(std::string name, float weight);
    void clearMorphPreview(const std::string &name);
    void clearMorphPreviews();
    void setPose(const mmd::VpdPose *pose);
    void setPhysicsEnabled(bool enabled);
    void setIkEnabled(bool enabled);
    void setFrame(float frame);
    void reset();
    [[nodiscard]] mmd::AnimatedModelFrame evaluate(float deltaSeconds = 0.0F, bool gpuSkinning = false);
    [[nodiscard]] const mmd::PmxModel &model() const noexcept;

  private:
    void rebuildMotion();

    const mmd::PmxModel *model_{};
    std::unique_ptr<mmd::MmdAnimator> animator_;
    std::unique_ptr<mmd::MmdPhysics> physics_;
    const mmd::VmdMotion *motion_{};
    mmd::VmdMotion previewMotion_;
    std::unordered_map<std::string, float> morphPreviews_;
    float frame_{};
    bool physicsEnabled_{true};
    bool ikEnabled_{true};
};

} // namespace pmxer
