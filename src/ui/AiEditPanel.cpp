#include "AiEditPanel.hpp"
#include "EditorPanels.hpp"
#include "ai/CodexClient.hpp"
#include "editor/AiEditExecutor.hpp"
#include "editor/TailoredGarment.hpp"
#include "editor/UiStatus.hpp"
#include "i18n/Language.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <exception>
#include <future>
#include <imgui.h>
#include <numeric>
#include <optional>

namespace pmxer {
struct AiEditState {
    ai::CodexClient client;
    std::array<char, 8193> prompt{};
    std::array<char, 129> model{};
    std::optional<ai::EditRegion> region;
    std::optional<ai::EditPlan> plan;
    std::unique_ptr<DocumentSession> preview;
    std::future<std::unique_ptr<DocumentSession>> previewTask;
    bool discardTask{};
    ai::GarmentDesign garment;
    mmd::Float4 garmentColor{0.045F, 0.035F, 0.055F, 1};
    std::string message;
    bool checkedAccount{};
    bool wholeModel{};
    bool completeMaterials{true};
    std::size_t targetFaceCount{};
    std::string targetNames;
};

namespace {
void copyCamera(DocumentSession &destination, const DocumentSession &source) {
    destination.ui.cameraTarget = source.ui.cameraTarget;
    destination.ui.cameraYaw = source.ui.cameraYaw;
    destination.ui.cameraPitch = source.ui.cameraPitch;
    destination.ui.cameraDistance = source.ui.cameraDistance;
    destination.ui.orthographic = source.ui.orthographic;
}
void launchPreview(DocumentSession &session, AiEditState &state) {
    const auto target = ai::resolveTarget(session, *state.region, state.plan->target);
    state.targetFaceCount = target.faces.size();
    state.targetNames.clear();
    for (auto index : state.plan->target.materialIndices) {
        if (!state.targetNames.empty())
            state.targetNames += ", ";
        state.targetNames += session.document.model().materials[index].name;
    }
    auto prepared = ai::prepareEditPreview(session);
    auto region = ai::captureFaces(*prepared, state.region->faceIndices);
    region.allowMaterialExpansion = state.region->allowMaterialExpansion;
    state.discardTask = false;
    state.previewTask =
        std::async(std::launch::async, [prepared = std::move(prepared), region = std::move(region),
                                        plan = *state.plan]() mutable {
            const auto result = ai::applyEditPlan(*prepared, region, plan);
            if (!result.success)
                throw std::runtime_error(result.message);
            return std::move(prepared);
        });
    state.message.clear();
}
} // namespace

DocumentSession &aiDisplaySession(DocumentSession &session, WorkspaceUiState &workspace) {
    if (!workspace.showAi && workspace.ai) {
        if (workspace.ai->preview && workspace.ai->region &&
            workspace.ai->region->documentId == session.recoveryId)
            copyCamera(session, *workspace.ai->preview);
        workspace.ai->preview.reset();
    }
    if (workspace.ai && workspace.ai->preview && workspace.ai->region) {
        const auto &region = *workspace.ai->region;
        if (region.documentId == session.recoveryId && region.revision == session.revision)
            return *workspace.ai->preview;
        workspace.ai->preview.reset();
    }
    return session;
}

void drawAiEditPanel(DocumentSession *session, WorkspaceUiState &workspace) {
    if (!workspace.showAi) {
        if (workspace.ai) {
            workspace.ai->client.cancel();
            (void)workspace.ai->client.takePlan();
            workspace.ai->discardTask = true;
            if (session && workspace.ai->preview && workspace.ai->region &&
                workspace.ai->region->documentId == session->recoveryId)
                copyCamera(*session, *workspace.ai->preview);
            workspace.ai->preview.reset();
            workspace.ai->plan.reset();
        }
        return;
    }
    if (!workspace.ai)
        workspace.ai = std::make_shared<AiEditState>();
    auto &state = *workspace.ai;
    if (!state.checkedAccount) {
        state.client.connect(false);
        state.checkedAccount = true;
    }
    if (auto answer = state.client.takePlan()) {
        try {
            if (!session || !state.region || state.region->documentId != session->recoveryId ||
                state.region->revision != session->revision)
                throw std::runtime_error(
                    i18n::tr("The model changed. Capture the selection again."));
            state.plan = ai::parsePlan(*answer);
            if (state.plan->operations.empty()) {
                state.message = state.plan->summary;
                state.preview.reset();
            } else {
                launchPreview(*session, state);
            }
        } catch (const std::exception &error) {
            state.message = error.what();
            state.preview.reset();
            state.plan.reset();
        }
    }
    if (state.previewTask.valid() &&
        state.previewTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto prepared = state.previewTask.get();
            if (!state.discardTask && session && state.region &&
                state.region->documentId == session->recoveryId &&
                state.region->revision == session->revision) {
                copyCamera(*prepared, *session);
                state.preview = std::move(prepared);
            } else if (!state.discardTask)
                state.message = i18n::tr("The model changed. Capture the selection again.");
        } catch (const std::exception &error) {
            if (!state.discardTask)
                state.message = error.what();
            state.plan.reset();
        }
    }
    const bool preparing = state.previewTask.valid();
    const auto connection = state.client.state();
    ImGui::SetNextWindowSize({460, 600}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(i18n::windowTitle("AI edit"), &workspace.showAi)) {
        ImGui::End();
        return;
    }
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(i18n::tr("ChatGPT account"));
    if (connection.connected)
        ImGui::Text("%s", connection.account.c_str());
    ImGui::BeginDisabled(connection.busy || preparing);
    if (ImGui::Button(
            i18n::tr(connection.connected ? "Check connection" : "Continue with ChatGPT")))
        state.client.connect(true);
    ImGui::EndDisabled();
    if (connection.busy) {
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr("Cancel")))
            state.client.cancel();
        ImGui::TextUnformatted(i18n::tr(
            connection.authUrl.empty() ? "Working…" : "Complete sign-in in your browser."));
        if (!connection.authUrl.empty() && ImGui::Button(i18n::tr("Open sign-in browser")))
            (void)SDL_OpenURL(connection.authUrl.c_str());
    }
    if (preparing) {
        ImGui::TextUnformatted(i18n::tr("Preparing mesh preview…"));
        if (ImGui::Button(i18n::tr("Cancel"))) {
            state.discardTask = true;
            state.plan.reset();
            state.message = i18n::tr("Discard preview");
        }
    }
    if (!connection.error.empty()) {
        ImGui::TextColored({1, 0.6F, 0.4F, 1}, "%s", connection.error.c_str());
        ImGui::TextDisabled(
            "%s",
            i18n::tr(
                "Codex is needed for natural-language edits. Local clothing remains available."));
    }
    ImGui::SeparatorText(i18n::tr("Selected region"));
    if (!session) {
        ImGui::TextUnformatted(i18n::tr("Open a PMX model first."));
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    const auto capture = [&]() {
        if (!state.wholeModel) {
            auto region = ai::captureRegion(*session);
            region.allowMaterialExpansion = state.completeMaterials;
            return region;
        }
        std::vector<std::size_t> indices(session->document.faces().size());
        std::iota(indices.begin(), indices.end(), std::size_t{0});
        return ai::captureFaces(*session, indices);
    };
    ImGui::BeginDisabled(connection.busy || preparing || state.preview != nullptr);
    if (ImGui::Checkbox(i18n::tr("Use whole model as search area"), &state.wholeModel) &&
        state.wholeModel) {
        workspace.active = EditorWorkspace::model;
        workspace.showTransformView = false;
        applyWorkspacePolicy(*session, workspacePolicy(workspace.active));
        session->ui.aiRegionSelection = false;
    }
    ImGui::Checkbox(i18n::tr("Include complete target materials"), &state.completeMaterials);
    ImGui::TextUnformatted(i18n::tr("Roughly enclose the area. AI chooses the requested materials "
                                    "and preserves unrelated parts."));
    if (ImGui::Button(i18n::tr("Select region in viewport"))) {
        state.wholeModel = false;
        workspace.active = EditorWorkspace::model;
        workspace.showTransformView = false;
        applyWorkspacePolicy(*session, workspacePolicy(workspace.active));
        session->ui.selectionMode = ViewportSelectionMode::face;
        session->ui.viewportTool = ViewportTool::select;
        session->ui.aiRegionSelection = true;
    }
    if (session->ui.aiRegionSelection) {
        ImGui::RadioButton(i18n::tr("Rectangle"), &session->ui.aiSelectionShape, 0);
        ImGui::SameLine();
        ImGui::RadioButton(i18n::tr("Lasso"), &session->ui.aiSelectionShape, 1);
        ImGui::Checkbox(i18n::tr("Select through mesh"), &session->ui.aiSelectThrough);
        ImGui::TextDisabled("%s", i18n::tr("Drag over the target. Shift adds; Ctrl toggles."));
    }
    if (ImGui::Button(i18n::tr("Capture selection"))) {
        try {
            state.region = capture();
            state.message.clear();
            state.plan.reset();
        } catch (const std::exception &error) {
            state.message = error.what();
            state.region.reset();
        }
    }
    ImGui::EndDisabled();
    if (state.region && state.region->documentId == session->recoveryId)
        ImGui::Text(i18n::tr("Captured faces: %zu / vertices: %zu"), state.region->faces.size(),
                    state.region->vertices.size());
    ImGui::SeparatorText(i18n::tr("Local clothing"));
    ImGui::TextDisabled(
        "%s", i18n::tr("Built locally. No sign-in, downloads or extra provider required."));
    ImGui::BeginDisabled(connection.busy || preparing || state.preview != nullptr ||
                         session->hasPendingTransformEdit());
    if (ImGui::CollapsingHeader(i18n::tr("Garment settings"))) {
        ImGui::ColorEdit3(i18n::tr("Fabric color"), state.garmentColor.data());
        ImGui::ColorEdit3(i18n::tr("Trim color"), state.garment.trimColor.data());
        ImGui::ColorEdit3(i18n::tr("Ribbon color"), state.garment.accentColor.data());
        ImGui::SliderInt(i18n::tr("Skirt tiers"), &state.garment.tiers, 1, 4);
        ImGui::SliderFloat(i18n::tr("Skirt length"), &state.garment.skirtLength, 0.5F, 1.5F);
        float sleevePercent = state.garment.sleeveLength * 50;
        if (ImGui::SliderFloat(i18n::tr("Sleeve length"), &sleevePercent, 0, 105, "%.0f%%"))
            state.garment.sleeveLength = sleevePercent / 50;
        ImGui::SliderFloat(i18n::tr("Skirt fullness"), &state.garment.fullness, 0.6F, 1.6F);
        ImGui::SliderFloat(i18n::tr("Bow size"), &state.garment.bowSize, 0, 1.5F);
        ImGui::Checkbox(i18n::tr("Openwork lace trim"), &state.garment.lace);
    }
    if (ImGui::Button(i18n::tr("Preview Gothic Lolita outfit"))) {
        try {
            state.region = capture();
            ai::Operation operation;
            operation.type = ai::OperationType::generate;
            operation.shape = ai::Shape::dress;
            operation.color = state.garmentColor;
            operation.garment = state.garment;
            operation.garment.style = ai::GarmentStyle::gothicLolita;
            state.plan = ai::EditPlan{
                i18n::tr("Local Gothic Lolita outfit with the selected garment settings."),
                {operation},
                ai::suggestOutfitTarget(*session, *state.region)};
            workspace.active = EditorWorkspace::model;
            workspace.showTransformView = false;
            applyWorkspacePolicy(*session, workspacePolicy(workspace.active));
            launchPreview(*session, state);
        } catch (const std::exception &error) {
            state.message = i18n::tr(error.what());
            state.plan.reset();
        }
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText(i18n::tr("Describe the edit"));
    ImGui::TextUnformatted(i18n::tr(
        "Examples: upscale the mesh; change the outfit to a dress; lengthen only the hair."));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##ai-prompt", state.prompt.data(), state.prompt.size(), {-1, 100});
    if (ImGui::CollapsingHeader(i18n::tr("Model settings"))) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ai-model", i18n::tr("Use Codex default"), state.model.data(),
                                 state.model.size());
    }
    ImGui::TextDisabled("%s", i18n::tr("Selection bounds, material and bone names are sent to "
                                       "Codex. The PMX file stays local."));
    ImGui::BeginDisabled(connection.busy || preparing || !connection.connected ||
                         state.prompt[0] == '\0' || state.preview != nullptr ||
                         session->hasPendingTransformEdit());
    if (ImGui::Button(i18n::tr("Generate preview"))) {
        try {
            // Freeze the current selection and revision at request time, never use later selection
            // changes.
            state.region = capture();
            state.plan.reset();
            state.message.clear();
            state.client.generate(state.prompt.data(),
                                  ai::regionContext(*session, *state.region).dump(),
                                  state.model.data());
        } catch (const std::exception &error) {
            state.message = error.what();
        }
    }
    ImGui::EndDisabled();
    if (session->hasPendingTransformEdit())
        ImGui::TextUnformatted(i18n::tr("Finish or discard temporary transforms first."));
    if (!state.message.empty())
        ImGui::Text("%s", state.message.c_str());
    if (state.plan) {
        ImGui::SeparatorText(i18n::tr("Proposed edit"));
        ImGui::Text("%s", state.plan->summary.c_str());
        if (state.preview) {
            ImGui::Text(i18n::tr("Edit target: %zu faces (captured: %zu)"), state.targetFaceCount,
                        state.region->faces.size());
            if (!state.targetNames.empty())
                ImGui::Text("%s", state.targetNames.c_str());
        }
    }
    if (state.preview && state.region) {
        ImGui::TextUnformatted(i18n::tr("Preview only. The original model has not changed."));
        const bool current = state.region->documentId == session->recoveryId &&
                             state.region->revision == session->revision;
        ImGui::BeginDisabled(!current);
        if (ImGui::Button(i18n::tr("Apply edit"))) {
            const auto result = ai::acceptEditPreview(*session, *state.region, *state.preview);
            state.message =
                result.success ? i18n::tr("AI edit applied. Undo is available.") : result.message;
            if (result.success) {
                copyCamera(*session, *state.preview);
                state.preview.reset();
                state.plan.reset();
                state.region.reset();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr("Discard preview"))) {
            if (state.region->documentId == session->recoveryId)
                copyCamera(*session, *state.preview);
            state.preview.reset();
            state.plan.reset();
            state.region.reset();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", i18n::tr("Mesh subdivision, smoothing, inflation, transforms, solid "
                                       "colors and basic clothing generation. "
                                       "Texture upscaling and generation are not supported."));
    ImGui::TextDisabled(
        "%s",
        i18n::tr("Clothing targets are chosen within the captured area. Body surfaces missing "
                 "beneath clothing are not reconstructed."));
    ImGui::PopTextWrapPos();
    ImGui::End();
}
} // namespace pmxer
