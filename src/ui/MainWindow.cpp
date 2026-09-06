#include "MainWindow.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/SaveController.hpp"
#include "../platform/Log.hpp"

#include <mmd/pmx.hpp>

#include <exception>
#include <array>
#include <memory>
#include <string>
#include <vector>

#if PMXER_HAS_GUI
#include "EditorPanels.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace pmxer {

int runApplication(const std::filesystem::path *initialPath) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;
    SDL_Window *window = SDL_CreateWindow("pmxer", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }
    auto *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                           SDL_GPU_SHADERFORMAT_MSL,
                                       false, nullptr);
    if (device == nullptr || !SDL_ClaimWindowForGPUDevice(device, window)) {
        if (device != nullptr)
            SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::vector<std::unique_ptr<DocumentSession>> sessions;
    std::size_t activeSession{};
    if (initialPath != nullptr && !initialPath->empty()) {
        try {
            sessions.push_back(std::make_unique<DocumentSession>(mmd::pmx::load(*initialPath), *initialPath));
            sessions.back()->ui.openPath = initialPath->string();
        } catch (const std::exception &error) {
            log::error(error.what());
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo gpuInfo{};
    gpuInfo.Device = device;
    gpuInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
    ImGui_ImplSDLGPU3_Init(&gpuInfo);
    std::array<char, 1024> newSessionPath{};
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
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
        if (ImGui::Button("タブで開く") && newSessionPath[0] != '\0') {
            try {
                const std::filesystem::path path(newSessionPath.data());
                sessions.push_back(std::make_unique<DocumentSession>(mmd::pmx::load(path), path));
                activeSession = sessions.size() - 1;
                newSessionPath.fill('\0');
            } catch (const std::exception &error) {
                log::error(error.what());
            }
        }
        ImGui::End();
        if (!sessions.empty() && activeSession < sessions.size())
            drawEditorPanels(*sessions[activeSession]);
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
        if (commands != nullptr && SDL_AcquireGPUSwapchainTexture(commands, window, &swapchain, &width, &height) &&
            swapchain != nullptr) {
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commands);
            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            target.clear_color = {0.055F, 0.065F, 0.08F, 1.0F};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            auto *pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commands, pass);
            SDL_EndGPURenderPass(pass);
        }
        if (commands != nullptr)
            SDL_SubmitGPUCommandBuffer(commands);
    }
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace pmxer
#else

namespace pmxer {

int runApplication(const std::filesystem::path *) {
    return 0;
}

} // namespace pmxer
#endif
