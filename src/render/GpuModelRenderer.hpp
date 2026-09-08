#pragma once

#include "../editor/DocumentSession.hpp"

#include <mmd/animation.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

struct SDL_GPUCommandBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUDevice;
struct SDL_GPUIndexBufferBinding;
struct SDL_GPURenderPass;
struct SDL_GPUShader;
struct SDL_GPUBuffer;
struct SDL_GPUTexture;

namespace pmxer {

struct GpuTexturePreview {
    SDL_GPUTexture *texture{};
    std::uint32_t width{};
    std::uint32_t height{};
};

enum class TextureResourceState { loaded, missing, decodeFailed, uploadFailed };

struct TextureResourceStatus {
    std::size_t textureIndex{};
    TextureResourceState state{TextureResourceState::missing};
    std::filesystem::path resolvedPath;
};

struct RendererResourceSummary {
    std::size_t missingTextureCount{};
    std::size_t failedTextureCount{};
};

class GpuModelRenderer {
  public:
    GpuModelRenderer(SDL_GPUDevice *device, std::filesystem::path shaderDirectory,
                     std::filesystem::path resourceDirectory, std::uint32_t colorFormat);
    ~GpuModelRenderer();

    GpuModelRenderer(const GpuModelRenderer &) = delete;
    GpuModelRenderer &operator=(const GpuModelRenderer &) = delete;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] const char *error() const noexcept;
    [[nodiscard]] GpuTexturePreview
    texturePreview(const DocumentSession &session,
                   std::size_t index) const noexcept;
    [[nodiscard]] RendererResourceSummary
    resourceSummary(const DocumentSession &session) const noexcept;
    [[nodiscard]] std::span<const TextureResourceStatus>
    resourceStatuses(const DocumentSession &session) const noexcept;

    bool prepare(SDL_GPUCommandBuffer *commands, const mmd::PmxModel &model,
                 const mmd::AnimatedModelFrame *frame, std::uint64_t revision,
                 std::uint64_t resourceRevision, std::uint64_t frameRevision,
                 const mmd::PmxChangeSet &changes);
    void render(SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass,
                const DocumentSession &session,
                const mmd::AnimatedModelFrame *frame, float framebufferScale,
                std::uint32_t framebufferWidth,
                std::uint32_t framebufferHeight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pmxer
