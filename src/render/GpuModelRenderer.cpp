#include "GpuModelRenderer.hpp"

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
};

struct alignas(16) FrameUniforms {
    float viewProjection[16];
};

struct Basis {
    std::array<float, 3> right{};
    std::array<float, 3> up{};
    std::array<float, 3> forward{};
    std::array<float, 3> eye{};
};

float dot(const std::array<float, 3> &a, const std::array<float, 3> &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> cross(const std::array<float, 3> &a, const std::array<float, 3> &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

std::array<float, 3> normalize(std::array<float, 3> value) {
    const auto length = std::sqrt(dot(value, value));
    if (length <= std::numeric_limits<float>::epsilon())
        return {0.0F, 0.0F, 1.0F};
    for (auto &component : value)
        component /= length;
    return value;
}

Basis makeBasis(const EditorUiState &ui) {
    const auto cosPitch = std::cos(ui.cameraPitch);
    const auto sinPitch = std::sin(ui.cameraPitch);
    const auto cosYaw = std::cos(ui.cameraYaw);
    const auto sinYaw = std::sin(ui.cameraYaw);
    Basis basis;
    basis.eye = {ui.cameraTarget[0] + sinYaw * cosPitch * ui.cameraDistance,
                 ui.cameraTarget[1] + sinPitch * ui.cameraDistance,
                 ui.cameraTarget[2] + cosYaw * cosPitch * ui.cameraDistance};
    basis.forward = normalize({ui.cameraTarget[0] - basis.eye[0], ui.cameraTarget[1] - basis.eye[1],
                               ui.cameraTarget[2] - basis.eye[2]});
    basis.right = normalize(cross(basis.forward, {0.0F, 1.0F, 0.0F}));
    basis.up = normalize(cross(basis.right, basis.forward));
    return basis;
}

FrameUniforms makeUniforms(const EditorUiState &ui, float aspect) {
    const auto basis = makeBasis(ui);
    const auto nearPlane = std::max(0.01F, ui.cameraDistance * 0.001F);
    const auto farPlane = std::max(1000.0F, ui.cameraDistance * 100.0F);
    const auto verticalField = 0.75F;
    const auto focal = 1.0F / std::tan(verticalField * 0.5F);
    const auto xScale = focal / std::max(aspect, 0.001F);
    const auto zScale = farPlane / (farPlane - nearPlane);
    const auto zOffset = -nearPlane * farPlane / (farPlane - nearPlane);

    const auto view = std::array<float, 16>{
        basis.right[0], basis.right[1], basis.right[2], -dot(basis.right, basis.eye),
        basis.up[0], basis.up[1], basis.up[2], -dot(basis.up, basis.eye),
        basis.forward[0], basis.forward[1], basis.forward[2], -dot(basis.forward, basis.eye),
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    const auto projection = std::array<float, 16>{
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, focal, 0.0F, 0.0F,
        0.0F, 0.0F, zScale, zOffset,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
    FrameUniforms result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column) {
            result.viewProjection[row * 4 + column] =
                projection[row * 4 + 0] * view[column] + projection[row * 4 + 1] * view[4 + column] +
                projection[row * 4 + 2] * view[8 + column] + projection[row * 4 + 3] * view[12 + column];
        }
    return result;
}

std::vector<GpuVertex> makeVertices(const std::vector<mmd::PmxVertex> &vertices) {
    std::vector<GpuVertex> result;
    result.reserve(vertices.size());
    for (const auto &vertex : vertices)
        result.push_back({{vertex.position[0], vertex.position[1], vertex.position[2]},
                          {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                          {vertex.uv[0], vertex.uv[1]}});
    return result;
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

} // namespace

struct GpuModelRenderer::Impl {
    SDL_GPUDevice *device{};
    SDL_GPUShader *vertexShader{};
    SDL_GPUShader *fragmentShader{};
    SDL_GPUGraphicsPipeline *pipeline{};
    SDL_GPUBuffer *vertexBuffer{};
    SDL_GPUBuffer *indexBuffer{};
    std::vector<SDL_GPUTransferBuffer *> transfers;
    std::filesystem::path shaderDirectory;
    const mmd::PmxModel *model{};
    const mmd::AnimatedModelFrame *frame{};
    std::uint64_t revision{std::numeric_limits<std::uint64_t>::max()};
    std::size_t indexCount{};
    bool available{};
    std::string errorMessage;

    void clearBuffers() {
        if (vertexBuffer != nullptr) {
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
            vertexBuffer = nullptr;
        }
        if (indexBuffer != nullptr) {
            SDL_ReleaseGPUBuffer(device, indexBuffer);
            indexBuffer = nullptr;
        }
    }

    void clearTransfers() {
        for (auto *transfer : transfers)
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        transfers.clear();
    }

    ~Impl() {
        clearBuffers();
        clearTransfers();
        if (pipeline != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        if (vertexShader != nullptr)
            SDL_ReleaseGPUShader(device, vertexShader);
        if (fragmentShader != nullptr)
            SDL_ReleaseGPUShader(device, fragmentShader);
    }
};

GpuModelRenderer::GpuModelRenderer(SDL_GPUDevice *device, std::filesystem::path shaderDirectory,
                                   std::uint32_t colorFormat)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = device;
    impl_->shaderDirectory = std::move(shaderDirectory);
    const auto vertexPath = impl_->shaderDirectory / "model.vert.spv";
    const auto fragmentPath = impl_->shaderDirectory / "model.frag.spv";
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
    vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code_size = fragmentSize;
    fragmentInfo.code = fragmentCode;
    fragmentInfo.entrypoint = "mainPS";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
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
    const std::array<SDL_GPUVertexAttribute, 3> attributes{{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, normal)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GpuVertex, uv)},
    }};
    SDL_GPUColorTargetDescription target{};
    target.format = static_cast<SDL_GPUTextureFormat>(colorFormat);
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = impl_->vertexShader;
    pipelineInfo.fragment_shader = impl_->fragmentShader;
    pipelineInfo.vertex_input_state = {buffers.data(), 1, attributes.data(), 3};
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.rasterizer_state.enable_depth_clip = true;
    pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipelineInfo.target_info.color_target_descriptions = &target;
    pipelineInfo.target_info.num_color_targets = 1;
    impl_->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
    if (impl_->pipeline == nullptr) {
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

bool GpuModelRenderer::prepare(SDL_GPUCommandBuffer *commands, const mmd::PmxModel &model,
                               const mmd::AnimatedModelFrame *frame, std::uint64_t revision, bool dynamic) {
    if (!available() || commands == nullptr || model.indices.empty())
        return false;
    if (!dynamic && impl_->model == &model && impl_->frame == frame && impl_->revision == revision &&
        impl_->vertexBuffer != nullptr && impl_->indexBuffer != nullptr)
        return true;
    const auto &source = frame != nullptr && !frame->vertices.empty() ? frame->vertices : model.vertices;
    const auto vertices = makeVertices(source);
    if (vertices.empty())
        return false;
    impl_->clearTransfers();
    impl_->clearBuffers();
    SDL_GPUBufferCreateInfo vertexInfo{SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<Uint32>(vertices.size() * sizeof(GpuVertex)), 0};
    SDL_GPUBufferCreateInfo indexInfo{SDL_GPU_BUFFERUSAGE_INDEX, static_cast<Uint32>(model.indices.size() * sizeof(std::uint32_t)), 0};
    impl_->vertexBuffer = SDL_CreateGPUBuffer(impl_->device, &vertexInfo);
    impl_->indexBuffer = SDL_CreateGPUBuffer(impl_->device, &indexInfo);
    if (impl_->vertexBuffer == nullptr || impl_->indexBuffer == nullptr)
        return false;
    if (!uploadBuffer(impl_->device, commands, impl_->vertexBuffer, vertices.data(), vertices.size() * sizeof(GpuVertex),
                      impl_->transfers) ||
        !uploadBuffer(impl_->device, commands, impl_->indexBuffer, model.indices.data(),
                      model.indices.size() * sizeof(std::uint32_t), impl_->transfers))
        return false;
    impl_->model = &model;
    impl_->frame = frame;
    impl_->revision = revision;
    impl_->indexCount = model.indices.size();
    return true;
}

void GpuModelRenderer::render(SDL_GPUCommandBuffer *commands, SDL_GPURenderPass *pass,
                              const mmd::PmxModel &model, const EditorUiState &ui, float framebufferScale,
                              std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
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
    const auto uniforms = makeUniforms(ui, aspect);
    SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_GPUViewport viewport{x, y, width, height, 0.0F, 1.0F};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_Rect scissor{static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height)};
    SDL_SetGPUScissor(pass, &scissor);
    SDL_BindGPUGraphicsPipeline(pass, impl_->pipeline);
    const SDL_GPUBufferBinding vertexBinding{impl_->vertexBuffer, 0};
    const SDL_GPUBufferBinding indexBinding{impl_->indexBuffer, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    std::size_t indexBegin = 0;
    for (const auto &material : model.materials) {
        const auto available = impl_->indexCount - std::min(indexBegin, impl_->indexCount);
        const auto count = std::min(available, static_cast<std::size_t>(material.indexCount));
        if (count == 0)
            continue;
        const auto color = std::array<float, 4>{material.diffuse[0], material.diffuse[1], material.diffuse[2],
                                                material.diffuse[3]};
        SDL_PushGPUFragmentUniformData(commands, 0, color.data(), sizeof(color));
        SDL_DrawGPUIndexedPrimitives(pass, static_cast<Uint32>(count), 1, static_cast<Uint32>(indexBegin), 0, 0);
        indexBegin += count;
    }
    if (indexBegin < impl_->indexCount) {
        const auto color = std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F};
        SDL_PushGPUFragmentUniformData(commands, 0, color.data(), sizeof(color));
        SDL_DrawGPUIndexedPrimitives(pass, static_cast<Uint32>(impl_->indexCount - indexBegin), 1,
                                     static_cast<Uint32>(indexBegin), 0, 0);
    }
    (void)model;
}

} // namespace pmxer
