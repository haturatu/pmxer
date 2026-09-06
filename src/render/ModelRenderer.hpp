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

struct GpuModelState {
    std::uint64_t vertexBuffer{};
    std::uint64_t indexBuffer{};
    std::uint64_t materialBuffer{};
    std::uint64_t boneBuffer{};
    std::uint64_t morphBuffer{};
    std::size_t dirtyVertexBegin{};
    std::size_t dirtyVertexEnd{};
};

class ModelRenderer {
  public:
    void setModel(const mmd::PmxModel &model);
    void invalidate(RenderInvalidation invalidation) noexcept;
    void setFrame(const mmd::AnimatedModelFrame *frame) noexcept;
    [[nodiscard]] RenderStatistics statistics() const noexcept;
    [[nodiscard]] bool hasModel() const noexcept;
    [[nodiscard]] const GpuModelState &resources() const noexcept;

  private:
    const mmd::PmxModel *model_{};
    const mmd::AnimatedModelFrame *frame_{};
    RenderInvalidation invalidation_;
    GpuModelState resources_;
};

} // namespace pmxer
