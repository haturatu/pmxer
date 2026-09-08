#include "GpuModelRenderer.hpp"

#include "Camera.hpp"
#include "ImageDecoder.hpp"

#include "../platform/Log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace pmxer {
namespace {

struct GpuVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float additionalUv1[2];
};

struct alignas(16) FrameUniforms {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> edgeParameters{};
};

struct alignas(16) MaterialUniforms {
    std::array<float, 4> diffuse{};
    std::array<float, 4> textureMultiply{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> textureAdd{};
    std::array<float, 4> sphereMultiply{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> sphereAdd{};
    std::array<float, 4> toonMultiply{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> toonAdd{};
    std::array<float, 4> edgeColor{};
    std::array<float, 4> materialModes{};
    std::array<float, 4> textureFlags{};
};

FrameUniforms makeUniforms(const EditorUiState &ui, float aspect) {
    const CameraState camera{ui.cameraTarget, ui.cameraYaw, ui.cameraPitch, ui.cameraDistance, ui.orthographic};
    const auto matrices = makeCameraMatrices(camera, aspect);
    FrameUniforms result{};
    result.viewProjection = matrices.viewProjection;
    return result;
}

std::vector<GpuVertex> makeVertices(const std::vector<mmd::PmxVertex> &vertices) {
    std::vector<GpuVertex> result;
    result.reserve(vertices.size());
    for (const auto &vertex : vertices)
        result.push_back({{vertex.position[0], vertex.position[1], vertex.position[2]},
                          {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                          {vertex.uv[0], vertex.uv[1]},
                          {vertex.additionalUv[0][0], vertex.additionalUv[0][1]}});
    return result;
}

std::array<std::uint8_t, 64U * 4U> makeSharedToonFallback(std::size_t index) {
    constexpr std::array<std::array<std::uint8_t, 3>, 10> shadows{{
        {52, 52, 56}, {64, 51, 51}, {51, 59, 68}, {58, 51, 66}, {50, 65, 56},
        {69, 60, 47}, {47, 64, 68}, {67, 48, 60}, {58, 58, 47}, {44, 44, 48},
    }};
    const auto shadow = shadows[index % shadows.size()];
    std::array<std::uint8_t, 64U * 4U> result{};
    for (std::size_t row = 0; row < 64U; ++row) {
        const auto eased = row * row;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const auto range = 255U - shadow[channel];
            result[row * 4U + channel] = static_cast<std::uint8_t>(
                shadow[channel] + range * eased / (63U * 63U));
        }
        result[row * 4U + 3U] = 255;
    }
    return result;
}

std::array<std::uint8_t, 2U * 2U * 4U> makeNeutralTexture() {
    return {{184, 190, 198, 255, 132, 138, 146, 255,
             132, 138, 146, 255, 184, 190, 198, 255}};
}

std::array<std::uint8_t, 64U * 4U> makeNeutralToonFallback() {
    std::array<std::uint8_t, 64U * 4U> result{};
    for (std::size_t row = 0; row < 64U; ++row) {
        const auto value = static_cast<std::uint8_t>(96U + row * 159U / 63U);
        result[row * 4U] = value;
        result[row * 4U + 1U] = value;
        result[row * 4U + 2U] = value;
        result[row * 4U + 3U] = 255;
    }
    return result;
}

bool containsItem(const std::vector<SelectionItem> &items,
                  const SelectionItem &item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

bool uploadBuffer(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, SDL_GPUBuffer *buffer,
                  const void *data, std::size_t size, std::vector<SDL_GPUTransferBuffer *> &transfers) {
    if (size == 0)
        return true;
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(size);
    auto *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == nullptr)
        return false;
    auto *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    auto *copy = SDL_BeginGPUCopyPass(commands);
    if (copy == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_GPUTransferBufferLocation source{transfer, 0};
    SDL_GPUBufferRegion destination{buffer, 0, static_cast<Uint32>(size)};
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
    transfers.push_back(transfer);
    return true;
}

SDL_GPUTexture *uploadTexture(SDL_GPUDevice *device, SDL_GPUCommandBuffer *commands, const unsigned char *pixels,
                              int width, int height, std::vector<SDL_GPUTransferBuffer *> &transfers) {
    if (pixels == nullptr || width <= 0 || height <= 0)
        return nullptr;
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = static_cast<Uint32>(width);
    textureInfo.height = static_cast<Uint32>(height);
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    auto *texture = SDL_CreateGPUTexture(device, &textureInfo);
    if (texture == nullptr)
        return nullptr;

    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(size);
    auto *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == nullptr) {
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    auto *mapped = static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(device, transfer, false));
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    std::memcpy(mapped, pixels, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    auto *copy = SDL_BeginGPUCopyPass(commands);
    if (copy == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    SDL_GPUTextureTransferInfo source{transfer, 0, static_cast<Uint32>(width), static_cast<Uint32>(height)};
    SDL_GPUTextureRegion destination{texture, 0, 0, 0, 0, 0, static_cast<Uint32>(width), static_cast<Uint32>(height), 1};
    SDL_UploadToGPUTexture(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
    transfers.push_back(transfer);
    return texture;
}

struct ShaderSelection {
    std::filesystem::path vertex;
    std::filesystem::path fragment;
    SDL_GPUShaderFormat format{SDL_GPU_SHADERFORMAT_SPIRV};
};

ShaderSelection selectShaders(SDL_GPUDevice *device, const std::filesystem::path &directory) {
    const auto *driver = SDL_GetGPUDeviceDriver(device);
    std::string extension = "spv";
    auto format = SDL_GPU_SHADERFORMAT_SPIRV;
    if (driver != nullptr && std::strcmp(driver, "direct3d12") == 0) {
        extension = "dxil";
        format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (driver != nullptr && std::strcmp(driver, "metal") == 0) {
        extension = "msl";
        format = SDL_GPU_SHADERFORMAT_MSL;
    }
    return {directory / ("model.vert." + extension), directory / ("model.frag." + extension), format};
}

} // namespace

struct GpuModelRenderer::Impl {
    SDL_GPUDevice *device{};
    SDL_GPUShader *vertexShader{};
    SDL_GPUShader *fragmentShader{};
    SDL_GPUGraphicsPipeline *pipeline{};
    SDL_GPUGraphicsPipeline *singleSidedPipeline{};
    SDL_GPUGraphicsPipeline *edgePipeline{};
    SDL_GPUBuffer *vertexBuffer{};
    SDL_GPUBuffer *indexBuffer{};
    SDL_GPUSampler *baseSampler{};
    SDL_GPUSampler *sphereSampler{};
    SDL_GPUSampler *toonSampler{};
    SDL_GPUTexture *defaultTexture{};
    SDL_GPUTexture *neutralTexture{};
    SDL_GPUTexture *blackTexture{};
    SDL_GPUTexture *toonFallbackTexture{};
    std::vector<SDL_GPUTexture *> textures;
    std::vector<std::array<std::uint32_t, 2>> textureSizes;
    std::vector<TextureResourceStatus> textureStatus;
    std::array<SDL_GPUTexture *, 10> sharedToons{};
    std::vector<SDL_GPUTexture *> retiredTextures;
    std::vector<SDL_GPUTransferBuffer *> transfers;
    std::filesystem::path shaderDirectory;
    std::filesystem::path resourceDirectory;
    const mmd::PmxModel *model{};
    const mmd::AnimatedModelFrame *frame{};
    std::uint64_t revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t resourceRevision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t frameRevision{std::numeric_limits<std::uint64_t>::max()};
    std::size_t vertexCapacity{};
    std::size_t indexCapacity{};
    std::size_t indexCount{};
    bool available{};
    std::string errorMessage;

    void clearBuffers() {
        if (vertexBuffer != nullptr) {
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
            vertexBuffer = nullptr;
            vertexCapacity = 0;
        }
        if (indexBuffer != nullptr) {
            SDL_ReleaseGPUBuffer(device, indexBuffer);
            indexBuffer = nullptr;
            indexCapacity = 0;
        }
    }

    void clearTransfers() {
        for (auto *transfer : transfers)
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        transfers.clear();
    }

    void retireTextures() {
        for (auto *texture : textures)
            if (texture != nullptr)
                retiredTextures.push_back(texture);
        textures.clear();
        textureSizes.clear();
        for (auto *&texture : sharedToons) {
            if (texture != nullptr)
                retiredTextures.push_back(texture);
            texture = nullptr;
        }
        if (defaultTexture != nullptr) {
            retiredTextures.push_back(defaultTexture);
            defaultTexture = nullptr;
        }
        if (neutralTexture != nullptr) {
            retiredTextures.push_back(neutralTexture);
            neutralTexture = nullptr;
        }
        if (blackTexture != nullptr) {
            retiredTextures.push_back(blackTexture);
            blackTexture = nullptr;
        }
        if (toonFallbackTexture != nullptr) {
            retiredTextures.push_back(toonFallbackTexture);
            toonFallbackTexture = nullptr;
        }
    }

    void releaseRetiredTextures() {
        for (auto *texture : retiredTextures)
            SDL_ReleaseGPUTexture(device, texture);
        retiredTextures.clear();
    }

    void clearTextures() {
        retireTextures();
        releaseRetiredTextures();
    }

    bool prepareTextures(SDL_GPUCommandBuffer *commands, const mmd::PmxModel &model) {
        retireTextures();
        const auto createSampler = [&](SDL_GPUSamplerAddressMode addressMode) {
            SDL_GPUSamplerCreateInfo samplerInfo{};
            samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
            samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
            samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            samplerInfo.address_mode_u = addressMode;
            samplerInfo.address_mode_v = addressMode;
            samplerInfo.address_mode_w = addressMode;
            return SDL_CreateGPUSampler(device, &samplerInfo);
        };
        if (baseSampler == nullptr)
            baseSampler = createSampler(SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
        if (sphereSampler == nullptr)
            sphereSampler = createSampler(SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);
        if (toonSampler == nullptr)
            toonSampler = createSampler(SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);
        const unsigned char white[] = {255, 255, 255, 255};
        defaultTexture = uploadTexture(device, commands, white, 1, 1, transfers);
        const auto neutral = makeNeutralTexture();
        neutralTexture = uploadTexture(device, commands, neutral.data(), 2, 2, transfers);
        const unsigned char black[] = {0, 0, 0, 255};
        blackTexture = uploadTexture(device, commands, black, 1, 1, transfers);
        const auto toonFallback = makeNeutralToonFallback();
        toonFallbackTexture = uploadTexture(device, commands, toonFallback.data(), 1, 64, transfers);
        if (defaultTexture == nullptr || neutralTexture == nullptr || blackTexture == nullptr ||
            toonFallbackTexture == nullptr || baseSampler == nullptr || sphereSampler == nullptr ||
            toonSampler == nullptr)
            return false;

        textures.resize(model.textures.size(), nullptr);
        textureSizes.resize(model.textures.size());
        textureStatus.assign(model.textures.size(), {});
        for (std::size_t index = 0; index < model.textures.size(); ++index) {
            textureStatus[index].textureIndex = index;
            const auto path = mmd::pmx::resolveTexturePath(model, index);
            textureStatus[index].resolvedPath = path;
            if (path.empty() || !std::filesystem::exists(path)) {
                textureStatus[index].state = TextureResourceState::missing;
                continue;
            }
            const auto image = decodeImage(path);
            if (!image) {
                textureStatus[index].state = TextureResourceState::decodeFailed;
                const auto message = std::string{"Texture decode failed: "} + path.generic_string() + ": " + image.error;
                log::warn(message.c_str());
                continue;
            }
            textures[index] = uploadTexture(device, commands, image.rgba.data(),
                                            static_cast<int>(image.width), static_cast<int>(image.height), transfers);
            if (textures[index] != nullptr)
                textureSizes[index] = {image.width, image.height};
            textureStatus[index].state = textures[index] != nullptr
                                             ? TextureResourceState::loaded
                                             : TextureResourceState::uploadFailed;
        }
        for (std::size_t index = 0; index < sharedToons.size(); ++index) {
            const auto number = index + 1U;
            const auto filename = std::string{"toon"} + (number < 10U ? "0" : "") +
                                  std::to_string(number) + ".bmp";
            const auto image = decodeImage(resourceDirectory / "toon" / filename);
            if (image)
                sharedToons[index] = uploadTexture(device, commands, image.rgba.data(),
                                                   static_cast<int>(image.width), static_cast<int>(image.height),
                                                   transfers);
            if (sharedToons[index] == nullptr) {
                const auto gradient = makeSharedToonFallback(index);
                sharedToons[index] = uploadTexture(device, commands, gradient.data(), 1, 64, transfers);
            }
        }
        return true;
    }

    ~Impl() {
        clearBuffers();
        clearTextures();
        clearTransfers();
        if (baseSampler != nullptr)
            SDL_ReleaseGPUSampler(device, baseSampler);
        if (sphereSampler != nullptr)
            SDL_ReleaseGPUSampler(device, sphereSampler);
        if (toonSampler != nullptr)
            SDL_ReleaseGPUSampler(device, toonSampler);
        if (pipeline != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        if (singleSidedPipeline != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device, singleSidedPipeline);
        if (edgePipeline != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device, edgePipeline);
        if (vertexShader != nullptr)
            SDL_ReleaseGPUShader(device, vertexShader);
        if (fragmentShader != nullptr)
            SDL_ReleaseGPUShader(device, fragmentShader);
    }
};

GpuModelRenderer::GpuModelRenderer(SDL_GPUDevice *device, std::filesystem::path shaderDirectory,
                                   std::filesystem::path resourceDirectory, std::uint32_t colorFormat)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = device;
    impl_->shaderDirectory = std::move(shaderDirectory);
    impl_->resourceDirectory = std::move(resourceDirectory);
    const auto shaders = selectShaders(device, impl_->shaderDirectory);
    const auto vertexPath = shaders.vertex;
    const auto fragmentPath = shaders.fragment;
    size_t vertexSize = 0;
    size_t fragmentSize = 0;
    auto *vertexCode = static_cast<Uint8 *>(SDL_LoadFile(vertexPath.string().c_str(), &vertexSize));
    auto *fragmentCode = static_cast<Uint8 *>(SDL_LoadFile(fragmentPath.string().c_str(), &fragmentSize));
    if (vertexCode == nullptr || fragmentCode == nullptr) {
        impl_->errorMessage = "GPU model shaders are unavailable";
        if (vertexCode != nullptr)
            SDL_free(vertexCode);
        if (fragmentCode != nullptr)
            SDL_free(fragmentCode);
        return;
    }
    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code_size = vertexSize;
    vertexInfo.code = vertexCode;
    vertexInfo.entrypoint = "mainVS";
    vertexInfo.format = shaders.format;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code_size = fragmentSize;
    fragmentInfo.code = fragmentCode;
    fragmentInfo.entrypoint = "mainPS";
    fragmentInfo.format = shaders.format;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 3;
    fragmentInfo.num_uniform_buffers = 1;
    impl_->vertexShader = SDL_CreateGPUShader(device, &vertexInfo);
    impl_->fragmentShader = SDL_CreateGPUShader(device, &fragmentInfo);
    SDL_free(vertexCode);
    SDL_free(fragmentCode);
    if (impl_->vertexShader == nullptr || impl_->fragmentShader == nullptr) {
        impl_->errorMessage = SDL_GetError();
        return;
    }

    const std::array<SDL_GPUVertexBufferDescription, 1> buffers{{{0, sizeof(GpuVertex),
                                                                   SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}}};
    const std::array<SDL_GPUVertexAttribute, 4> attributes{{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, normal)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, uv)},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, additionalUv1)},
    }};
    SDL_GPUColorTargetDescription target{};
    target.format = static_cast<SDL_GPUTextureFormat>(colorFormat);
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = impl_->vertexShader;
    pipelineInfo.fragment_shader = impl_->fragmentShader;
    pipelineInfo.vertex_input_state = {buffers.data(), 1, attributes.data(), 4};
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.rasterizer_state.enable_depth_clip = true;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipelineInfo.target_info.color_target_descriptions = &target;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pipelineInfo.target_info.has_depth_stencil_target = true;
    impl_->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
    if (impl_->pipeline == nullptr) {
        impl_->errorMessage = SDL_GetError();
        return;
    }
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    impl_->singleSidedPipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
    if (impl_->singleSidedPipeline == nullptr) {
        impl_->errorMessage = SDL_GetError();
        return;
    }
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
    pipelineInfo.depth_stencil_state.enable_depth_write = false;
    impl_->edgePipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
    if (impl_->edgePipeline == nullptr) {
        impl_->errorMessage = SDL_GetError();
        return;
    }
    impl_->available = true;
}

GpuModelRenderer::~GpuModelRenderer() = default;

bool GpuModelRenderer::available() const noexcept {
    return impl_ != nullptr && impl_->available;
}

const char *GpuModelRenderer::error() const noexcept {
    return impl_ == nullptr ? "GPU renderer is unavailable" : impl_->errorMessage.c_str();
}

GpuTexturePreview GpuModelRenderer::texturePreview(
    const DocumentSession &session, std::size_t index) const noexcept {
    if (impl_ == nullptr || impl_->model != &session.document.model() ||
        impl_->resourceRevision != session.resourceRevision ||
        index >= impl_->textures.size() ||
        index >= impl_->textureSizes.size())
        return {};
    return {impl_->textures[index], impl_->textureSizes[index][0],
            impl_->textureSizes[index][1]};
}

RendererResourceStatus GpuModelRenderer::resourceStatus(
    const DocumentSession &session) const {
    RendererResourceStatus result;
    if (impl_ == nullptr || impl_->model != &session.document.model() ||
        impl_->resourceRevision != session.resourceRevision)
        return result;
    result.textures = impl_->textureStatus;
    for (const auto &texture : result.textures) {
        if (texture.state == TextureResourceState::missing)
            ++result.missingTextureCount;
        else if (texture.state == TextureResourceState::decodeFailed ||
                 texture.state == TextureResourceState::uploadFailed)
            ++result.failedTextureCount;
    }
    return result;
}

bool GpuModelRenderer::prepare(SDL_GPUCommandBuffer *commands, const mmd::PmxModel &model,
                               const mmd::AnimatedModelFrame *frame, std::uint64_t revision,
                               std::uint64_t resourceRevision, std::uint64_t frameRevision,
                               const mmd::PmxChangeSet &changes) {
    if (!available() || commands == nullptr || model.indices.empty())
        return false;
    const auto &source = frame != nullptr && !frame->vertices.empty() ? frame->vertices : model.vertices;
    if (source.empty())
        return false;
    const auto modelChanged = impl_->model != &model;
    const auto documentChanged = impl_->revision != revision;
    const auto resourcesChanged = impl_->resourceRevision != resourceRevision;
    const auto frameChanged = impl_->frameRevision != frameRevision;
    const auto topologyChanged = modelChanged || (documentChanged && changes.topologyChanged) ||
                                 impl_->indexBuffer == nullptr ||
                                 impl_->indexCount != model.indices.size();
    const auto verticesChanged = modelChanged || frameChanged ||
                                 (documentChanged && (changes.topologyChanged || !changes.vertices.empty())) ||
                                 impl_->vertexBuffer == nullptr;
    const auto texturesChanged = modelChanged || resourcesChanged || (documentChanged && changes.texturesChanged) ||
                                 impl_->defaultTexture == nullptr;
    const auto vertexBytes = source.size() * sizeof(GpuVertex);
    const auto indexBytes = model.indices.size() * sizeof(std::uint32_t);
    impl_->clearTransfers();
    impl_->releaseRetiredTextures();
    if (texturesChanged && !impl_->prepareTextures(commands, model))
        return false;
    const auto ensureBuffer = [&](SDL_GPUBuffer *&buffer, std::size_t &capacity, SDL_GPUBufferUsageFlags usage,
                                  std::size_t required) {
        if (buffer != nullptr && capacity >= required)
            return true;
        if (buffer != nullptr)
            SDL_ReleaseGPUBuffer(impl_->device, buffer);
        SDL_GPUBufferCreateInfo info{};
        info.usage = usage;
        info.size = static_cast<Uint32>(required);
        buffer = SDL_CreateGPUBuffer(impl_->device, &info);
        if (buffer == nullptr) {
            capacity = 0;
            return false;
        }
        capacity = required;
        return true;
    };
    if (!ensureBuffer(impl_->vertexBuffer, impl_->vertexCapacity, SDL_GPU_BUFFERUSAGE_VERTEX, vertexBytes) ||
        !ensureBuffer(impl_->indexBuffer, impl_->indexCapacity, SDL_GPU_BUFFERUSAGE_INDEX, indexBytes)) {
        impl_->clearBuffers();
        return false;
    }
    if (verticesChanged) {
        const auto vertices = makeVertices(source);
        if (!uploadBuffer(impl_->device, commands, impl_->vertexBuffer, vertices.data(), vertexBytes, impl_->transfers))
            return false;
    }
    if (topologyChanged &&
        !uploadBuffer(impl_->device, commands, impl_->indexBuffer, model.indices.data(), indexBytes, impl_->transfers))
        return false;
    impl_->model = &model;
    impl_->frame = frame;
    impl_->revision = revision;
    impl_->resourceRevision = resourceRevision;
    impl_->frameRevision = frameRevision;
    impl_->indexCount = model.indices.size();
    return true;
}

void GpuModelRenderer::render(SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass,
                              const DocumentSession &session,
                              const mmd::AnimatedModelFrame *frame,
                              float framebufferScale, std::uint32_t framebufferWidth,
                              std::uint32_t framebufferHeight) {
    const auto &model = session.document.model();
    const auto &ui = session.ui;
    if (!available() || commands == nullptr || pass == nullptr || impl_->vertexBuffer == nullptr ||
        impl_->indexBuffer == nullptr || impl_->indexCount == 0 || !ui.viewportVisible)
        return;
    const auto x = std::max(0.0F, ui.viewportX * framebufferScale);
    const auto y = std::max(0.0F, ui.viewportY * framebufferScale);
    const auto width = std::min(std::max(0.0F, ui.viewportWidth * framebufferScale),
                                static_cast<float>(framebufferWidth) - x);
    const auto height = std::min(std::max(0.0F, ui.viewportHeight * framebufferScale),
                                 static_cast<float>(framebufferHeight) - y);
    if (width <= 1.0F || height <= 1.0F)
        return;
    const auto aspect = width / height;
    const auto frameUniforms = makeUniforms(ui, aspect);
    SDL_GPUViewport viewport{x, y, width, height, 0.0F, 1.0F};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_Rect scissor{static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height)};
    SDL_SetGPUScissor(pass, &scissor);
    const SDL_GPUBufferBinding vertexBinding{impl_->vertexBuffer, 0};
    const SDL_GPUBufferBinding indexBinding{impl_->indexBuffer, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    std::size_t indexBegin = 0;
    for (std::size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
        const auto &material = model.materials[materialIndex];
        const auto available = impl_->indexCount - std::min(indexBegin, impl_->indexCount);
        const auto count = std::min(available, static_cast<std::size_t>(material.indexCount));
        if (count == 0)
            continue;
        const auto handle = session.document.materialHandle(materialIndex);
        const SelectionItem item{SelectionKind::material, handle.domain,
                                 handle.id, handle.generation};
        const auto hidden = containsItem(ui.hiddenMaterials, item);
        const auto isolated = !ui.isolatedMaterials.empty() &&
                              !containsItem(ui.isolatedMaterials, item);
        if (hidden || isolated) {
            indexBegin += count;
            continue;
        }
        const auto *animated = frame != nullptr && materialIndex < frame->materials.size()
                                   ? &frame->materials[materialIndex]
                                   : nullptr;
        const auto &diffuse = animated != nullptr ? animated->diffuse : material.diffuse;
        MaterialUniforms uniforms;
        uniforms.diffuse = {diffuse[0], diffuse[1], diffuse[2],
                            ui.xray ? diffuse[3] * 0.28F : diffuse[3]};
        if (animated != nullptr) {
            uniforms.textureMultiply = {animated->textureMultiply[0], animated->textureMultiply[1],
                                        animated->textureMultiply[2], animated->textureMultiply[3]};
            uniforms.textureAdd = {animated->textureAdd[0], animated->textureAdd[1], animated->textureAdd[2],
                                   animated->textureAdd[3]};
            uniforms.sphereMultiply = {animated->sphereMultiply[0], animated->sphereMultiply[1],
                                       animated->sphereMultiply[2], animated->sphereMultiply[3]};
            uniforms.sphereAdd = {animated->sphereAdd[0], animated->sphereAdd[1], animated->sphereAdd[2],
                                  animated->sphereAdd[3]};
            uniforms.toonMultiply = {animated->toonMultiply[0], animated->toonMultiply[1], animated->toonMultiply[2],
                                     animated->toonMultiply[3]};
            uniforms.toonAdd = {animated->toonAdd[0], animated->toonAdd[1], animated->toonAdd[2],
                                animated->toonAdd[3]};
        }
        const auto hovered = ui.viewportHover && *ui.viewportHover == item;
        const auto selected = session.selection.contains(item);
        uniforms.materialModes = {
            static_cast<float>(material.sphereMode),
            static_cast<float>(material.toonMode), 0.0F,
            selected ? 0.32F : (hovered ? 0.18F : 0.0F)};
        const auto textureLoaded = [&](std::int32_t index) {
            return index >= 0 && static_cast<std::size_t>(index) < impl_->textureStatus.size() &&
                   impl_->textureStatus[static_cast<std::size_t>(index)].state ==
                       TextureResourceState::loaded;
        };
        const auto baseTextureFor = [&](std::int32_t index) -> SDL_GPUTexture * {
            if (index < 0)
                return impl_->defaultTexture;
            return textureLoaded(index) ? impl_->textures[static_cast<std::size_t>(index)]
                                        : impl_->neutralTexture;
        };
        const auto sphereTextureFor = [&](std::int32_t index) -> SDL_GPUTexture * {
            if (index < 0)
                return impl_->defaultTexture;
            return textureLoaded(index)
                       ? impl_->textures[static_cast<std::size_t>(index)]
                       : (material.sphereMode == 2U ? impl_->blackTexture
                                                    : impl_->defaultTexture);
        };
        const auto toonTextureFor = [&](std::int32_t index) -> SDL_GPUTexture * {
            if (index < 0)
                return impl_->defaultTexture;
            return textureLoaded(index) ? impl_->textures[static_cast<std::size_t>(index)]
                                        : impl_->toonFallbackTexture;
        };
        uniforms.textureFlags[0] = material.textureIndex >= 0 &&
                                           !textureLoaded(material.textureIndex)
                                       ? 1.0F
                                       : 0.0F;
        const std::array<SDL_GPUTextureSamplerBinding, 3> bindings{{
            {baseTextureFor(material.textureIndex), impl_->baseSampler},
            {sphereTextureFor(material.sphereTextureIndex), impl_->sphereSampler},
            {material.toonMode == 0 ? toonTextureFor(material.toonTextureIndex)
                                    : (material.toonTextureIndex >= 0 && material.toonTextureIndex < 10
                                           ? impl_->sharedToons[static_cast<std::size_t>(material.toonTextureIndex)]
                                           : impl_->toonFallbackTexture),
             impl_->toonSampler},
        }};
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
        const auto &edgeColor = animated != nullptr ? animated->edgeColor : material.edgeColor;
        const auto edgeSize = animated != nullptr ? animated->edgeSize : material.edgeSize;
        if ((material.drawFlags & 0x10U) != 0U && edgeSize > 0.0F && edgeColor[3] > 0.0F) {
            auto edgeFrame = frameUniforms;
            edgeFrame.edgeParameters[0] = edgeSize;
            auto edgeMaterial = uniforms;
            edgeMaterial.edgeColor = {edgeColor[0], edgeColor[1], edgeColor[2], edgeColor[3]};
            if (ui.xray)
                edgeMaterial.edgeColor[3] *= 0.28F;
            edgeMaterial.materialModes[2] = 1.0F;
            SDL_PushGPUVertexUniformData(commands, 0, &edgeFrame, sizeof(edgeFrame));
            SDL_PushGPUFragmentUniformData(commands, 0, &edgeMaterial, sizeof(edgeMaterial));
            SDL_BindGPUGraphicsPipeline(pass, impl_->edgePipeline);
            SDL_DrawGPUIndexedPrimitives(pass, static_cast<Uint32>(count), 1,
                                         static_cast<Uint32>(indexBegin), 0, 0);
        }
        SDL_PushGPUVertexUniformData(commands, 0, &frameUniforms, sizeof(frameUniforms));
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_BindGPUGraphicsPipeline(pass, (material.drawFlags & 0x01U) != 0U
                                              ? impl_->pipeline
                                              : impl_->singleSidedPipeline);
        SDL_DrawGPUIndexedPrimitives(pass, static_cast<Uint32>(count), 1, static_cast<Uint32>(indexBegin), 0, 0);
        indexBegin += count;
    }
    if (indexBegin < impl_->indexCount) {
        SDL_BindGPUGraphicsPipeline(pass, impl_->pipeline);
        MaterialUniforms uniforms;
        uniforms.diffuse = {1.0F, 1.0F, 1.0F, ui.xray ? 0.28F : 1.0F};
        SDL_PushGPUVertexUniformData(commands, 0, &frameUniforms, sizeof(frameUniforms));
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
        const std::array<SDL_GPUTextureSamplerBinding, 3> bindings{{
            {impl_->defaultTexture, impl_->baseSampler},
            {impl_->defaultTexture, impl_->sphereSampler},
            {impl_->defaultTexture, impl_->toonSampler},
        }};
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
        SDL_DrawGPUIndexedPrimitives(pass, static_cast<Uint32>(impl_->indexCount - indexBegin), 1,
                                     static_cast<Uint32>(indexBegin), 0, 0);
    }
}

} // namespace pmxer
