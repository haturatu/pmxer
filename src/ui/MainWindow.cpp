#include "MainWindow.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/RecoveryController.hpp"
#include "../editor/SaveController.hpp"
#include "../platform/Log.hpp"
#include "../platform/ResourceLocator.hpp"

#include <mmd/pmx.hpp>

#include <exception>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if PMXER_HAS_GUI
#include "EditorPanels.hpp"
#include "../platform/FileDialog.hpp"
#include "../render/GpuModelRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace pmxer {

int runApplication(const EditCommand &options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log::error(SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("pmxer", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        log::error(SDL_GetError());
        SDL_Quit();
        return 1;
    }
    bool enableGpuDebug = false;
#if !defined(NDEBUG)
    enableGpuDebug = true;
#endif
    const char *rendererName = options.renderer == "auto" ? nullptr : options.renderer.c_str();
    auto *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                           SDL_GPU_SHADERFORMAT_MSL,
                                       enableGpuDebug || options.gpuDebug, rendererName);
    if (device == nullptr || !SDL_ClaimWindowForGPUDevice(device, window)) {
        log::error(SDL_GetError());
        if (device != nullptr)
            SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const auto releaseWindow = [&]() {
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
    };

    std::vector<std::unique_ptr<DocumentSession>> sessions;
    std::size_t activeSession{};
    const auto loadSession = [&](const std::filesystem::path &path) {
        try {
            auto session = std::make_unique<DocumentSession>(mmd::pmx::load(path), path);
            if (auto recovery = loadRecovery(path))
                session->recoveryModel = std::move(*recovery);
            sessions.push_back(std::move(session));
            sessions.back()->ui.openPath = path.string();
            sessions.back()->previewPhysics = options.physics;
            sessions.back()->previewIk = !options.safeMode;
        } catch (const std::exception &error) {
            log::error(error.what());
        }
    };
    for (const auto &path : options.documents)
        loadSession(path);
    auto untitledRecoveries = findUntitledRecoveries();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    const auto basePath = SDL_GetBasePath();
    const auto resourceDirectory = resolveResourceDirectory(
        options.resourceDirectory,
        basePath == nullptr ? std::filesystem::path{} : std::filesystem::path(basePath));
    const auto fontPath = resolveUiFont(options.font, resourceDirectory);
    if (fontPath.empty()) {
        log::warn("UI font was not found; Japanese text may be unavailable");
        io.Fonts->AddFontDefault();
    } else {
        const auto fontName = fontPath.string();
        if (io.Fonts->AddFontFromFileTTF(fontName.c_str(), options.fontSize) == nullptr)
            log::warn("UI font could not be loaded");
        else
            log::info(("Loaded UI font: " + fontName).c_str());
    }
    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        log::error("GUI platform backend initialization failed");
        ImGui::DestroyContext();
        releaseWindow();
        return 1;
    }
    ImGui_ImplSDLGPU3_InitInfo gpuInfo{};
    gpuInfo.Device = device;
    gpuInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    gpuInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    gpuInfo.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    gpuInfo.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!ImGui_ImplSDLGPU3_Init(&gpuInfo)) {
        log::error("GUI GPU backend initialization failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        releaseWindow();
        return 1;
    }
    std::filesystem::path shaderDirectory = std::filesystem::path(PMXER_SHADER_DIRECTORY);
    const auto installedShaderDirectory = resourceDirectory.parent_path() / "shaders";
    if (!std::filesystem::is_directory(shaderDirectory) && std::filesystem::is_directory(installedShaderDirectory))
        shaderDirectory = installedShaderDirectory;
    GpuModelRenderer gpuModelRenderer(device, shaderDirectory, gpuInfo.ColorTargetFormat);
    if (!gpuModelRenderer.available())
        log::warn(gpuModelRenderer.error());
    std::array<char, 1024> newSessionPath{};
    FileDialog fileDialog(window);
    bool running = true;
    bool quitRequested = false;
    bool quitPromptOpened = false;
    bool recoveryPromptOpened = false;
    std::size_t quitSessionIndex = 0;
    SDL_GPUTexture *depthTexture = nullptr;
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;
    const auto ensureDepthTexture = [&](Uint32 width, Uint32 height) {
        if (depthTexture != nullptr && depthWidth == width && depthHeight == height)
            return true;
        if (depthTexture != nullptr) {
            SDL_ReleaseGPUTexture(device, depthTexture);
            depthTexture = nullptr;
        }
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        depthTexture = SDL_CreateGPUTexture(device, &info);
        if (depthTexture == nullptr) {
            depthWidth = 0;
            depthHeight = 0;
            return false;
        }
        depthWidth = width;
        depthHeight = height;
        return true;
    };
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                quitRequested = true;
                quitPromptOpened = false;
                quitSessionIndex = 0;
            }
        }
        if (const auto result = fileDialog.takeResult()) {
            try {
                if (result->save) {
                    if (!sessions.empty() && activeSession < sessions.size())
                        sessions[activeSession]->ui.status = saveDocument(*sessions[activeSession], result->path).success
                                                                  ? "保存しました"
                                                                  : "保存に失敗しました";
                } else {
                    loadSession(result->path);
                    if (!sessions.empty())
                        activeSession = sessions.size() - 1;
                }
            } catch (const std::exception &error) {
                log::error(error.what());
            }
        }
        if (const auto error = fileDialog.takeError())
            log::error(error->c_str());
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (!untitledRecoveries.empty() && !recoveryPromptOpened) {
            ImGui::OpenPopup("回復情報");
            recoveryPromptOpened = true;
        }
        if (recoveryPromptOpened && ImGui::BeginPopupModal("回復情報", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("未保存の無題ドキュメントがあります。");
            for (std::size_t index = 0; index < untitledRecoveries.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::Text("%s", untitledRecoveries[index].path.filename().string().c_str());
                ImGui::SameLine();
                if (ImGui::Button("復元")) {
                    auto session = std::make_unique<DocumentSession>(std::move(untitledRecoveries[index].model));
                    session->commands.markDirty();
                    session->modified = true;
                    session->previewPhysics = options.physics;
                    session->previewIk = !options.safeMode;
                    sessions.push_back(std::move(session));
                    discardRecoveryFile(untitledRecoveries[index].path);
                    untitledRecoveries.erase(untitledRecoveries.begin() + static_cast<std::ptrdiff_t>(index));
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("破棄")) {
                    discardRecoveryFile(untitledRecoveries[index].path);
                    untitledRecoveries.erase(untitledRecoveries.begin() + static_cast<std::ptrdiff_t>(index));
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (untitledRecoveries.empty()) {
                recoveryPromptOpened = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (quitRequested && !quitPromptOpened) {
            while (quitSessionIndex < sessions.size() && !sessions[quitSessionIndex]->modified)
                ++quitSessionIndex;
            if (quitSessionIndex >= sessions.size()) {
                quitRequested = false;
                running = false;
            } else {
                ImGui::OpenPopup("未保存の変更##quit");
                quitPromptOpened = true;
            }
        }
        if (quitPromptOpened && quitSessionIndex < sessions.size() &&
            ImGui::BeginPopupModal("未保存の変更##quit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto &session = *sessions[quitSessionIndex];
            const auto title = session.path.empty() ? std::string{"無題"} : session.path.filename().string();
            ImGui::Text("%s に未保存の変更があります。", title.c_str());
            if (ImGui::Button("保存して終了")) {
                if (session.path.empty()) {
                    session.ui.status = "保存先を指定してください。終了はキャンセルされました";
                    quitRequested = false;
                    quitPromptOpened = false;
                    ImGui::CloseCurrentPopup();
                } else if (saveDocument(session).success) {
                    ++quitSessionIndex;
                    quitPromptOpened = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    session.ui.status = "保存に失敗しました。終了はキャンセルされました";
                    quitRequested = false;
                    quitPromptOpened = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("破棄して終了")) {
                // Discard is provisional until every document accepts quitting.
                // A later cancellation must leave this document dirty.
                ++quitSessionIndex;
                quitPromptOpened = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル")) {
                quitRequested = false;
                quitPromptOpened = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::DockSpaceOverViewport();
        ImGui::Begin("ドキュメント");
        if (ImGui::BeginTabBar("document-tabs")) {
            for (std::size_t i = 0; i < sessions.size(); ++i) {
                const auto &path = sessions[i]->path;
                const auto title = path.empty() ? "無題" : path.filename().string();
                const auto label = title + "##document" + std::to_string(i);
                if (ImGui::BeginTabItem(label.c_str())) {
                    activeSession = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::InputText("新しいPMX", newSessionPath.data(), newSessionPath.size());
        ImGui::SameLine();
        if (ImGui::Button("ファイルを選択") && !fileDialog.busy())
            (void)fileDialog.open();
        if (ImGui::Button("タブで開く") && newSessionPath[0] != '\0') {
            try {
                const std::filesystem::path path(newSessionPath.data());
                loadSession(path);
                if (!sessions.empty())
                    activeSession = sessions.size() - 1;
                newSessionPath.fill('\0');
            } catch (const std::exception &error) {
                log::error(error.what());
            }
        }
        ImGui::End();
        if (!sessions.empty() && activeSession < sessions.size())
            drawEditorPanels(*sessions[activeSession], fileDialog);
        else {
            ImGui::Begin("pmxer");
            ImGui::TextUnformatted("PMX ファイルを開いてください");
            ImGui::End();
        }
        ImGui::Render();

        SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUTexture *swapchain = nullptr;
        Uint32 width = 0;
        Uint32 height = 0;
        if (commands == nullptr) {
            log::warn(SDL_GetError());
        } else if (!SDL_AcquireGPUSwapchainTexture(commands, window, &swapchain, &width, &height)) {
            log::warn(SDL_GetError());
        } else if (swapchain != nullptr) {
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commands);
            if (!sessions.empty() && activeSession < sessions.size()) {
                auto &session = *sessions[activeSession];
                (void)gpuModelRenderer.prepare(commands, session.document.model(), session.ui.previewFrame,
                                               session.revision, session.preview.frameRevision, session.changes);
            }
            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            target.clear_color = {0.055F, 0.065F, 0.08F, 1.0F};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            const auto hasDepth = ensureDepthTexture(width, height);
            if (hasDepth) {
                SDL_GPUDepthStencilTargetInfo depthTarget{};
                depthTarget.texture = depthTexture;
                depthTarget.clear_depth = 1.0F;
                depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
                depthTarget.store_op = SDL_GPU_STOREOP_STORE;
                depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                auto *modelPass = SDL_BeginGPURenderPass(commands, &target, 1, &depthTarget);
                if (modelPass == nullptr) {
                    log::warn(SDL_GetError());
                } else {
                    const auto scale = ImGui::GetDrawData()->FramebufferScale.x;
                    if (!sessions.empty() && activeSession < sessions.size()) {
                        auto &session = *sessions[activeSession];
                        gpuModelRenderer.render(commands, modelPass, session.document.model(), session.ui.previewFrame,
                                                session.ui, scale, width, height);
                    }
                    SDL_EndGPURenderPass(modelPass);
                }
            } else {
                log::warn("depth texture is unavailable; model rendering skipped");
            }
            SDL_GPUColorTargetInfo overlayTarget = target;
            overlayTarget.load_op = SDL_GPU_LOADOP_LOAD;
            auto *overlayPass = SDL_BeginGPURenderPass(commands, &overlayTarget, 1, nullptr);
            if (overlayPass == nullptr) {
                log::warn(SDL_GetError());
            } else {
                ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commands, overlayPass);
                SDL_EndGPURenderPass(overlayPass);
            }
        }
        if (commands != nullptr && !SDL_SubmitGPUCommandBuffer(commands))
            log::error(SDL_GetError());
    }
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (depthTexture != nullptr)
        SDL_ReleaseGPUTexture(device, depthTexture);
    releaseWindow();
    return 0;
}

} // namespace pmxer
#else

namespace pmxer {

int runApplication(const EditCommand &) {
    return 0;
}

} // namespace pmxer
#endif
