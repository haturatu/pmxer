#pragma once

#include "../editor/DocumentSession.hpp"

#include <mmd/animation.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

struct SDL_GPUCommandBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUDevice;
struct SDL_GPUIndexBufferBinding;
struct SDL_GPURenderPass;
struct SDL_GPUShader;
struct SDL_GPUBuffer;

namespace pmxer {

class GpuModelRenderer {
  public:
    GpuModelRenderer(SDL_GPUDevice *device, std::filesystem::path shaderDirectory, std::uint32_t colorFormat);
    ~GpuModelRenderer();

    GpuModelRenderer(const GpuModelRenderer &) = delete;
    GpuModelRenderer &operator=(const GpuModelRenderer &) = delete;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] const char *error() const noexcept;

    bool prepare(SDL_GPUCommandBuffer *commands, const mmd::PmxModel &model,
                 const mmd::AnimatedModelFrame *frame, std::uint64_t revision, std::uint64_t frameRevision,
                 const mmd::PmxChangeSet &changes, bool dynamic);
    void render(SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass, const mmd::PmxModel &model,
                const mmd::AnimatedModelFrame *frame, const EditorUiState &ui, float framebufferScale,
                std::uint32_t framebufferWidth,
                std::uint32_t framebufferHeight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pmxer
