#pragma once

#include <mmd/animation.hpp>

#include <cstddef>
#include <cstdint>

namespace pmxer {

struct RenderInvalidation {
    bool topology{};
    bool materials{};
    bool bones{};
    bool morphs{};
    bool textures{};
    bool physics{};
};

struct RenderStatistics {
    std::size_t vertices{};
    std::size_t indices{};
    std::size_t materials{};
    std::size_t bones{};
};

class ModelRenderer {
  public:
    void setModel(const mmd::PmxModel &model);
    void invalidate(RenderInvalidation invalidation) noexcept;
    void setFrame(const mmd::AnimatedModelFrame *frame) noexcept;
    [[nodiscard]] RenderStatistics statistics() const noexcept;
    [[nodiscard]] bool hasModel() const noexcept;

  private:
    const mmd::PmxModel *model_{};
    const mmd::AnimatedModelFrame *frame_{};
    RenderInvalidation invalidation_;
};

} // namespace pmxer

