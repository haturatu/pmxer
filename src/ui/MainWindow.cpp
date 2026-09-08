#include "MainWindow.hpp"
#include "UiSemantics.hpp"
#include "WorkspaceLayout.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/RecoveryController.hpp"
#include "../editor/SaveController.hpp"
#include "../platform/Log.hpp"
#include "../platform/Paths.hpp"
#include "../platform/ResourceLocator.hpp"

#include <mmd/pmx.hpp>
#include <mmd/vmd.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if PMXER_HAS_GUI
#include "EditorPanels.hpp"
#include "UiAutomation.hpp"
#include "../automation/AutomationProtocol.hpp"
#include "../platform/FileDialog.hpp"
#include "../preview/PreviewController.hpp"
#include "../render/GpuModelRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <imgui.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <imgui_internal.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace pmxer {
namespace {

void buildDefaultDockLayout(ImGuiID dockspaceId) {
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID center = dockspaceId;
    const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22F, nullptr, &center);
    const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30F, nullptr, &center);
    const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25F, nullptr, &center);
    auto leftTop = left;
    const auto leftBottom = ImGui::DockBuilderSplitNode(leftTop, ImGuiDir_Down, 0.72F, nullptr, &leftTop);

    ImGui::DockBuilderDockWindow("ビューポート", center);
    ImGui::DockBuilderDockWindow("ドキュメント", leftTop);
    ImGui::DockBuilderDockWindow("アウトライナー", leftBottom);
    ImGui::DockBuilderDockWindow("インスペクター", right);
    ImGui::DockBuilderDockWindow("診断", bottom);
    ImGui::DockBuilderDockWindow("参照", bottom);
    ImGui::DockBuilderDockWindow("差分", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

std::string automationState(const DocumentSession &session, SDL_Window *window,
                            std::size_t activeSession) {
    int width{};
    int height{};
    (void)SDL_GetWindowSizeInPixels(window, &width, &height);
    const auto &model = session.document.model();
    return "{\"window\":{\"width\":" + std::to_string(width) +
           ",\"height\":" + std::to_string(height) +
           "},\"active_session\":" + std::to_string(activeSession) +
           ",\"document\":{\"modified\":" +
           (session.modified ? std::string{"true"} : std::string{"false"}) +
           ",\"revision\":" + std::to_string(session.revision) +
           ",\"resource_revision\":" +
           std::to_string(session.resourceRevision) +
           ",\"vertices\":" + std::to_string(model.vertices.size()) +
           ",\"materials\":" + std::to_string(model.materials.size()) +
           ",\"bones\":" + std::to_string(model.bones.size()) +
           ",\"morphs\":" + std::to_string(model.morphs.size()) +
           "},\"selection_count\":" +
           std::to_string(session.selection.items().size()) +
           ",\"status\":" + automation::escapeJson(session.ui.status) +
           "}";
}

bool parseAutomationFloat(std::string_view text, float &value) {
    std::istringstream stream{std::string{text}};
    if (!(stream >> value))
        return false;
    std::string extra;
    return !(stream >> extra);
}

int automationMouseButton(std::string_view text) {
    if (text == "left" || text == "0")
        return 0;
    if (text == "right" || text == "1")
        return 1;
    if (text == "middle" || text == "2")
        return 2;
    return -1;
}

ImGuiKey automationKey(std::string_view text) {
    std::string key(text);
    std::ranges::transform(key, key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (key == "ctrl" || key == "control")
        return ImGuiMod_Ctrl;
    if (key == "shift")
        return ImGuiMod_Shift;
    if (key == "alt")
        return ImGuiMod_Alt;
    if (key == "super" || key == "meta")
        return ImGuiMod_Super;
    if (key == "tab")
        return ImGuiKey_Tab;
    if (key == "enter" || key == "return")
        return ImGuiKey_Enter;
    if (key == "escape" || key == "esc")
        return ImGuiKey_Escape;
    if (key == "backspace")
        return ImGuiKey_Backspace;
    if (key == "delete" || key == "del")
        return ImGuiKey_Delete;
    if (key == "home")
        return ImGuiKey_Home;
    if (key == "end")
        return ImGuiKey_End;
    if (key == "left")
        return ImGuiKey_LeftArrow;
    if (key == "right")
        return ImGuiKey_RightArrow;
    if (key == "up")
        return ImGuiKey_UpArrow;
    if (key == "down")
        return ImGuiKey_DownArrow;
    if (key.size() == 1U && key[0] >= 'a' && key[0] <= 'z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (key[0] - 'a'));
    if (key.size() == 1U && key[0] >= '0' && key[0] <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key[0] - '0'));
    return ImGuiKey_None;
}

AutomationResult handleAutomationInput(std::string_view operation,
                                        std::string_view target,
                                        std::string_view value) {
    auto &io = ImGui::GetIO();
    if (operation == "mouse" || operation == "mouse-move") {
        float x{};
        float y{};
        if (!parseAutomationFloat(target, x) ||
            !parseAutomationFloat(value, y))
            return {false, "invalid_value"};
        io.AddMousePosEvent(x, y);
        return {};
    }
    if (operation == "mouse-down" || operation == "mouse-up") {
        const auto button = automationMouseButton(target);
        if (button < 0)
            return {false, "invalid_value"};
        io.AddMouseButtonEvent(button, operation == "mouse-down");
        return {};
    }
    if (operation == "key-down" || operation == "key-up") {
        const auto key = automationKey(target);
        if (key == ImGuiKey_None)
            return {false, "invalid_value"};
        io.AddKeyEvent(key, operation == "key-down");
        return {};
    }
    if (operation == "text" || operation == "text-input") {
        io.AddInputCharactersUTF8(std::string(target).c_str());
        return {};
    }
    if (operation == "wheel") {
        float x{};
        float y{};
        if (!parseAutomationFloat(target, x) ||
            !parseAutomationFloat(value, y))
            return {false, "invalid_value"};
        io.AddMouseWheelEvent(x, y);
        return {};
    }
    return {false, "unknown_input"};
}

} // namespace

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
    if (const auto *driver = SDL_GetGPUDeviceDriver(device); driver != nullptr)
        log::info((std::string{"SDL_GPU driver: "} + driver).c_str());

    const auto releaseWindow = [&]() {
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
    };

    std::vector<std::unique_ptr<DocumentSession>> sessions;
    std::size_t activeSession{};
    std::optional<std::size_t> requestedActiveSession;
    UiAutomationRegistry automationRegistry;
    UiAutomationServer automationServer;
    std::vector<std::filesystem::path> recentFiles;
    const auto loadSession = [&](const std::filesystem::path &path) {
        try {
            auto session = std::make_unique<DocumentSession>(mmd::pmx::load(path), path);
            if (auto recovery = loadRecovery(path)) {
                session->recoveryModel = std::move(*recovery);
                session->recoveryFile = recoveryPath(path);
            }
            sessions.push_back(std::move(session));
            sessions.back()->automation = &automationRegistry;
            sessions.back()->ui.openPath = path.string();
            recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
            recentFiles.insert(recentFiles.begin(), path);
            if (recentFiles.size() > 8U)
                recentFiles.resize(8U);
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
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    const auto layoutPath = workspaceLayoutPath();
    WorkspaceLayout savedLayout;
    const auto layoutResult = loadWorkspaceLayout(layoutPath, savedLayout);
    if (layoutResult == WorkspaceLayoutLoadResult::loaded)
        ImGui::LoadIniSettingsFromMemory(savedLayout.imguiIni.data(),
                                         savedLayout.imguiIni.size());
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
    auto gpuModelRenderer = std::make_unique<GpuModelRenderer>(
        device, shaderDirectory, resourceDirectory, gpuInfo.ColorTargetFormat);
    if (!gpuModelRenderer->available())
        log::warn(gpuModelRenderer->error());
    if (options.automation) {
        if (!automationServer.start(options.automationSocket)) {
            log::error("UI automation endpoint could not be started");
            gpuModelRenderer.reset();
            ImGui_ImplSDLGPU3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            releaseWindow();
            return 1;
        }
        log::info((std::string{"UI automation endpoint: "} +
                   options.automationSocket.string()).c_str());
    }
    std::array<char, 1024> newSessionPath{};
    FileDialog fileDialog(window);
    bool running = true;
    bool quitRequested = false;
    bool quitPromptOpened = false;
    bool recoveryPromptOpened = false;
    std::size_t quitSessionIndex = 0;
    std::vector<bool> quitDiscarded;
    std::optional<std::string> pendingCloseSession;
    std::optional<std::string> closeAfterSaveSession;
    bool closePromptOpened = false;
    WorkspaceUiState workspace;
    if (layoutResult == WorkspaceLayoutLoadResult::legacy ||
        layoutResult == WorkspaceLayoutLoadResult::unsupportedVersion ||
        layoutResult == WorkspaceLayoutLoadResult::corrupt) {
        if (std::filesystem::exists(layoutPath) && !backupWorkspaceLayout(layoutPath))
            log::warn("workspace layout backup could not be created");
        ImGui::ClearIniSettings();
        workspace.status = layoutResult == WorkspaceLayoutLoadResult::legacy
                               ? "旧レイアウトを更新しました"
                               : "レイアウトを初期化しました";
    }
    bool initializeDockLayout = layoutResult != WorkspaceLayoutLoadResult::loaded;
    bool layoutNeedsSave = initializeDockLayout;
    SDL_GPUTexture *depthTexture = nullptr;
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;
    automationRegistry.setKeyHandler([&](std::string_view key) {
        std::string normalized(key);
        std::ranges::transform(normalized, normalized.begin(),
                               [](unsigned char value) {
                                   return static_cast<char>(std::tolower(value));
                               });
        if (sessions.empty() || activeSession >= sessions.size())
            return std::string{"{\"ok\":false,\"error\":\"no_document\"}"};
        auto &session = *sessions[activeSession];
        if (normalized == "ctrl+z") {
            return session.undo()
                       ? std::string{"{\"ok\":true}"}
                       : std::string{"{\"ok\":false,\"error\":\"nothing_to_undo\"}"};
        }
        if (normalized == "ctrl+y") {
            return session.redo()
                       ? std::string{"{\"ok\":true}"}
                       : std::string{"{\"ok\":false,\"error\":\"nothing_to_redo\"}"};
        }
        if (normalized == "ctrl+s") {
            if (session.path.empty()) {
                if (!fileDialog.busy() && fileDialog.save({}, session.recoveryId))
                    return std::string{"{\"ok\":true,\"pending\":true}"};
                return std::string{"{\"ok\":false,\"error\":\"save_dialog_unavailable\"}"};
            }
            const auto saved = saveDocument(session).success;
            session.ui.status = saved ? "保存しました" : "保存に失敗しました";
            return saved ? std::string{"{\"ok\":true}"}
                         : std::string{"{\"ok\":false,\"error\":\"save_failed\"}"};
        }
        if (normalized == "ctrl+shift+s") {
            if (!fileDialog.busy() && fileDialog.save(session.path, session.recoveryId))
                return std::string{"{\"ok\":true,\"pending\":true}"};
            return std::string{"{\"ok\":false,\"error\":\"save_dialog_unavailable\"}"};
        }
        return std::string{"{\"ok\":false,\"error\":\"unsupported_key\"}"};
    });
    automationRegistry.setInputHandler(
        [](std::string_view operation, std::string_view target,
           std::string_view value) {
            return handleAutomationInput(operation, target, value);
        });
    const auto findSession = [&](std::string_view id) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < sessions.size(); ++index)
            if (sessions[index]->recoveryId == id)
                return index;
        return std::nullopt;
    };
    const auto loadDroppedPreview = [&](const std::filesystem::path &path) {
        if (sessions.empty() || activeSession >= sessions.size()) {
            log::warn("open a model before dropping a motion or pose");
            return;
        }
        auto &session = *sessions[activeSession];
        auto &preview = session.preview;
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(),
                               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        try {
            if (extension == ".vmd") {
                preview.motion = mmd::vmd::load(path);
                preview.pose.reset();
                session.ui.motionPath = path.string();
                if (preview.controller) {
                    preview.controller->setPose(nullptr);
                    preview.controller->setMotion(&*preview.motion);
                }
                session.ui.status = "モーションを読み込みました";
            } else if (extension == ".vpd") {
                preview.pose = mmd::vpd::load(path);
                preview.motion.reset();
                session.ui.posePath = path.string();
                if (preview.controller) {
                    preview.controller->setMotion(nullptr);
                    preview.controller->setPose(&*preview.pose);
                }
                session.ui.status = "ポーズを読み込みました";
            }
            if (preview.controller) {
                preview.frame = preview.controller->evaluate();
                ++preview.frameRevision;
                session.ui.previewFrame = &*preview.frame;
            }
            preview.accumulator = 0.0;
            preview.clockInitialized = false;
        } catch (const std::exception &error) {
            session.ui.status = error.what();
            log::error(error.what());
        }
    };
    const auto closeSession = [&](std::size_t index) {
        if (index >= sessions.size())
            return;
        const auto &session = *sessions[index];
        const auto recovery = session.recoveryFile.value_or(
            session.path.empty() ? recoveryPath({}, session.recoveryId)
                                 : recoveryPath(session.path));
        if (!discardRecoveryFile(recovery))
            log::warn("recovery file could not be discarded while closing a document");
        sessions.erase(sessions.begin() + static_cast<std::ptrdiff_t>(index));
        if (sessions.empty()) {
            activeSession = 0;
        } else if (activeSession > index) {
            --activeSession;
        } else if (activeSession >= sessions.size()) {
            activeSession = sessions.size() - 1U;
        }
    };
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
                quitDiscarded.assign(sessions.size(), false);
            } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                const auto path = std::filesystem::path(event.drop.data);
                auto extension = path.extension().string();
                std::ranges::transform(extension, extension.begin(),
                                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                if (extension == ".pmx") {
                    loadSession(path);
                    if (!sessions.empty())
                        activeSession = sessions.size() - 1U;
                } else if (extension == ".vmd" || extension == ".vpd") {
                    loadDroppedPreview(path);
                } else {
                    log::warn("unsupported dropped file type");
                }
            }
        }
        if (const auto result = fileDialog.takeResult()) {
            try {
                if (result->canceled) {
                    if (closeAfterSaveSession &&
                        *closeAfterSaveSession == result->context)
                        closeAfterSaveSession.reset();
                } else if (result->save) {
                    const auto target = result->context.empty()
                                            ? (activeSession < sessions.size()
                                                   ? std::optional<std::size_t>{activeSession}
                                                   : std::nullopt)
                                            : findSession(result->context);
                    if (target) {
                        const auto targetId = sessions[*target]->recoveryId;
                        const auto saved = saveDocument(*sessions[*target], result->path).success;
                        sessions[*target]->ui.status = saved ? "保存しました" : "保存に失敗しました";
                        if (saved && closeAfterSaveSession &&
                            *closeAfterSaveSession == targetId) {
                            closeAfterSaveSession.reset();
                            closeSession(*target);
                        }
                    } else {
                        log::warn("save dialog target document is no longer open");
                    }
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
        if (options.automation)
            automationRegistry.beginFrame();
        ui::beginFrame();
        if (!sessions.empty() && activeSession < sessions.size() &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W,
                            ImGuiInputFlags_RouteGlobal))
            pendingCloseSession = sessions[activeSession]->recoveryId;
        const auto dockspaceId = ImGui::DockSpaceOverViewport();
        if (initializeDockLayout || workspace.resetLayout) {
            if (workspace.resetLayout)
                ImGui::ClearIniSettings();
            buildDefaultDockLayout(dockspaceId);
            initializeDockLayout = false;
            workspace.resetLayout = false;
            layoutNeedsSave = true;
        }
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
                    untitledRecoveries[index].model.sourcePath.clear();
                    auto session = std::make_unique<DocumentSession>(std::move(untitledRecoveries[index].model));
                    session->automation = &automationRegistry;
                    session->commands.markDirty();
                    session->modified = true;
                    session->recoveryFile = untitledRecoveries[index].path;
                    session->previewPhysics = options.physics;
                    session->previewIk = !options.safeMode;
                    sessions.push_back(std::move(session));
                    untitledRecoveries.erase(untitledRecoveries.begin() + static_cast<std::ptrdiff_t>(index));
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("破棄")) {
                    if (!discardRecoveryFile(untitledRecoveries[index].path)) {
                        const auto message = std::string{"Failed to discard recovery file: "} +
                                             untitledRecoveries[index].path.string();
                        log::warn(message.c_str());
                    }
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
                for (std::size_t index = 0; index < quitDiscarded.size(); ++index) {
                    if (!quitDiscarded[index])
                        continue;
                    auto &session = *sessions[index];
                    const auto recovery = session.recoveryFile.value_or(
                        session.path.empty() ? recoveryPath({}, session.recoveryId) : recoveryPath(session.path));
                    (void)discardRecoveryFile(recovery);
                    session.recoveryFile.reset();
                }
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
                quitDiscarded[quitSessionIndex] = true;
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
        if (workspace.showDocuments) {
            if (ImGui::Begin("ドキュメント", &workspace.showDocuments)) {
                if (options.automation) {
                    const auto position = ImGui::GetWindowPos();
                    const auto size = ImGui::GetWindowSize();
                    automationRegistry.registerWindow(
                        "documents", "ドキュメント", static_cast<int>(position.x),
                        static_cast<int>(position.y), static_cast<int>(size.x),
                        static_cast<int>(size.y));
                }
                if (ImGui::BeginTabBar("document-tabs")) {
                    for (std::size_t i = 0; i < sessions.size(); ++i) {
                        const auto &path = sessions[i]->path;
                        const auto title = (path.empty() ? std::string{"無題"} : path.filename().string()) +
                                           (sessions[i]->modified ? " *" : "");
                        const auto label = title + "##document" + std::to_string(i);
                        bool keepOpen = true;
                        const auto tabFlags =
                            requestedActiveSession &&
                                    *requestedActiveSession == i
                                ? ImGuiTabItemFlags_SetSelected
                                : ImGuiTabItemFlags_None;
                        if (ImGui::BeginTabItem(label.c_str(), &keepOpen,
                                                tabFlags)) {
                            activeSession = i;
                            if (options.automation) {
                                const auto minimum = ImGui::GetItemRectMin();
                                const auto maximum = ImGui::GetItemRectMax();
                                AutomationItem item;
                                item.window = "documents";
                                item.id = "documents/tab:" + sessions[i]->recoveryId;
                                item.role = "tab";
                                item.label = title;
                                item.selected = i == activeSession;
                                item.x = static_cast<int>(minimum.x);
                                item.y = static_cast<int>(minimum.y);
                                item.width = static_cast<int>(maximum.x - minimum.x);
                                item.height = static_cast<int>(maximum.y - minimum.y);
                                item.click = [&requestedActiveSession, i]() {
                                    requestedActiveSession = i;
                                };
                                automationRegistry.registerItem(std::move(item));
                            }
                            ImGui::EndTabItem();
                        } else if (options.automation) {
                            AutomationItem item;
                            item.window = "documents";
                            item.id = "documents/tab:" + sessions[i]->recoveryId;
                            item.role = "tab";
                            item.label = title;
                            item.selected = i == activeSession;
                            item.visible = true;
                            item.click = [&requestedActiveSession, i]() {
                                requestedActiveSession = i;
                            };
                            automationRegistry.registerItem(std::move(item));
                        }
                        if (!keepOpen && !pendingCloseSession)
                            pendingCloseSession = sessions[i]->recoveryId;
                    }
                    ImGui::EndTabBar();
                    if (requestedActiveSession &&
                        *requestedActiveSession == activeSession)
                        requestedActiveSession.reset();
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
            }
            ImGui::End();
        }
        if (pendingCloseSession && !closePromptOpened) {
            const auto target = findSession(*pendingCloseSession);
            if (!target) {
                pendingCloseSession.reset();
            } else if (!sessions[*target]->modified) {
                closeSession(*target);
                pendingCloseSession.reset();
            } else {
                ImGui::OpenPopup("未保存の変更##close-document");
                closePromptOpened = true;
            }
        }
        if (closePromptOpened &&
            ImGui::BeginPopupModal("未保存の変更##close-document", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto target = pendingCloseSession
                                    ? findSession(*pendingCloseSession)
                                    : std::nullopt;
            if (!target) {
                closePromptOpened = false;
                pendingCloseSession.reset();
                ImGui::CloseCurrentPopup();
            } else {
                auto &session = *sessions[*target];
                const auto title = session.path.empty()
                                       ? std::string{"無題"}
                                       : session.path.filename().string();
                ImGui::Text("%s に未保存の変更があります。", title.c_str());
                if (ImGui::Button("保存して閉じる")) {
                    if (session.path.empty()) {
                        if (!fileDialog.busy() &&
                            fileDialog.save({}, session.recoveryId)) {
                            closeAfterSaveSession = session.recoveryId;
                            closePromptOpened = false;
                            pendingCloseSession.reset();
                            ImGui::CloseCurrentPopup();
                        }
                    } else if (saveDocument(session).success) {
                        closeSession(*target);
                        closePromptOpened = false;
                        pendingCloseSession.reset();
                        ImGui::CloseCurrentPopup();
                    } else {
                        session.ui.status = "保存に失敗しました";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("破棄して閉じる")) {
                    closeSession(*target);
                    closePromptOpened = false;
                    pendingCloseSession.reset();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル")) {
                    closePromptOpened = false;
                    pendingCloseSession.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        DocumentSession *activeSessionPointer =
            !sessions.empty() && activeSession < sessions.size()
                ? sessions[activeSession].get()
                : nullptr;
        drawMainMenu(activeSessionPointer, fileDialog, workspace);
        if (!sessions.empty() && activeSession < sessions.size())
            drawEditorPanels(*sessions[activeSession], fileDialog,
                             gpuModelRenderer.get(), workspace);
        else {
            ImGui::Begin("ビューポート");
            ImGui::TextUnformatted("pmxer");
            ImGui::Spacing();
            ImGui::TextUnformatted("PMXモデルを開いて編集を開始");
            ImGui::Spacing();
            if (ui::button(ui::UiSemanticId::menuFile, "PMXファイルを開く", !fileDialog.busy()) &&
                !fileDialog.busy())
                (void)fileDialog.open();
            ImGui::TextDisabled("ここへ .pmx をドラッグ＆ドロップ");
            ImGui::SeparatorText("最近使ったファイル");
            if (recentFiles.empty()) {
                ImGui::TextDisabled("履歴はありません");
            } else {
                for (std::size_t index = 0; index < recentFiles.size(); ++index) {
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(recentFiles[index].string().c_str())) {
                        loadSession(recentFiles[index]);
                        if (!sessions.empty())
                            activeSession = sessions.size() - 1U;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::End();
        }
        if (workspace.requestCloseDocument) {
            workspace.requestCloseDocument = false;
            if (!sessions.empty() && activeSession < sessions.size())
                pendingCloseSession = sessions[activeSession]->recoveryId;
        }
        ImGui::Render();
        if (io.WantSaveIniSettings || layoutNeedsSave) {
            std::size_t size{};
            const auto *contents = ImGui::SaveIniSettingsToMemory(&size);
            if (!saveWorkspaceLayout(layoutPath, std::string_view(contents, size)))
                log::warn("workspace layout could not be saved");
            layoutNeedsSave = false;
            io.WantSaveIniSettings = false;
        }

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
                (void)gpuModelRenderer->prepare(commands, session.document.model(), session.ui.previewFrame,
                                                session.revision, session.resourceRevision,
                                                session.preview.frameRevision, session.changes);
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
                        gpuModelRenderer->render(commands, modelPass, session,
                                                 session.ui.previewFrame, scale,
                                                 width, height);
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
        bool frameSubmitted = false;
        if (commands != nullptr) {
            frameSubmitted = SDL_SubmitGPUCommandBuffer(commands);
            if (!frameSubmitted)
                log::error(SDL_GetError());
        }
        if (options.automation && frameSubmitted) {
            if (!sessions.empty() && activeSession < sessions.size())
                automationRegistry.setStateJson(
                    automationState(*sessions[activeSession], window,
                                    activeSession));
            else
                automationRegistry.setStateJson("{\"document\":null}");
            automationServer.processPending(
                [&](std::string_view request) {
                    return automationRegistry.handle(request);
                });
        }
    }
    std::size_t size{};
    const auto *contents = ImGui::SaveIniSettingsToMemory(&size);
    if (!saveWorkspaceLayout(layoutPath, std::string_view(contents, size)))
        log::warn("workspace layout could not be saved during shutdown");
    automationServer.stop();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (depthTexture != nullptr)
        SDL_ReleaseGPUTexture(device, depthTexture);
    gpuModelRenderer.reset();
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
