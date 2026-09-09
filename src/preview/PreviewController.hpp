#pragma once

#include <mmd/animation.hpp>
#include <mmd/document.hpp>
#include <mmd/physics.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace pmxer {

class PreviewController {
  public:
    explicit PreviewController(const mmd::PmxModel &model);
    explicit PreviewController(const mmd::PmxDocument &document);
    ~PreviewController();
    PreviewController(PreviewController &&) noexcept;
    PreviewController &operator=(PreviewController &&) noexcept;
    PreviewController(const PreviewController &) = delete;
    PreviewController &operator=(const PreviewController &) = delete;

    void setMotion(const mmd::VmdMotion *motion);
    void setMorphPreview(mmd::MorphHandle morph, float weight);
    void setMorphPreview(std::string name, float weight);
    void clearMorphPreview(mmd::MorphHandle morph);
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
    void applyMorphPreviews(mmd::AnimatedModelFrame &frame, bool gpuSkinning) const;

    struct MorphPreview {
        mmd::MorphHandle morph{};
        std::size_t index{};
        float weight{};
    };

    const mmd::PmxDocument *document_{};
    const mmd::PmxModel *model_{};
    std::unique_ptr<mmd::MmdAnimator> animator_;
    std::unique_ptr<mmd::MmdPhysics> physics_;
    const mmd::VmdMotion *motion_{};
    std::vector<MorphPreview> morphPreviews_;
    float frame_{};
    bool physicsEnabled_{true};
    bool ikEnabled_{true};
};

} // namespace pmxer
