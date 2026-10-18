#include "AiEditPanel.hpp"
#include "EditorPanels.hpp"
#include "ai/CodexClient.hpp"
#include "editor/AiEditExecutor.hpp"
#include "editor/UiStatus.hpp"
#include "i18n/Language.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <exception>
#include <imgui.h>
#include <optional>

namespace pmxer {
struct AiEditState {
    ai::CodexClient client;
    std::array<char, 8193> prompt{};
    std::array<char, 129> model{};
    std::optional<ai::EditRegion> region;
    std::optional<ai::EditPlan> plan;
    std::unique_ptr<DocumentSession> preview;
    std::string message;
    bool checkedAccount{};
};

DocumentSession &aiDisplaySession(DocumentSession &session, WorkspaceUiState &workspace) {
    if (!workspace.showAi && workspace.ai)
        workspace.ai->preview.reset();
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
                state.preview = ai::makeEditPreview(*session, *state.region, *state.plan);
                state.message.clear();
            }
        } catch (const std::exception &error) {
            state.message = error.what();
            state.preview.reset();
            state.plan.reset();
        }
    }
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
    ImGui::BeginDisabled(connection.busy);
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
    if (!connection.error.empty()) {
        ImGui::TextColored({1, 0.6F, 0.4F, 1}, "%s", connection.error.c_str());
    }
    ImGui::SeparatorText(i18n::tr("Selected region"));
    if (!session) {
        ImGui::TextUnformatted(i18n::tr("Open a PMX model first."));
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(connection.busy || state.preview != nullptr);
    if (ImGui::Button(i18n::tr("Select region in viewport"))) {
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
            state.region = ai::captureRegion(*session);
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
    ImGui::SeparatorText(i18n::tr("Describe the edit"));
    ImGui::TextUnformatted(
        i18n::tr("Examples: black loafers; a white shirt; a shorter pleated skirt."));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##ai-prompt", state.prompt.data(), state.prompt.size(), {-1, 100});
    if (ImGui::CollapsingHeader(i18n::tr("Model settings"))) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ai-model", i18n::tr("Use Codex default"), state.model.data(),
                                 state.model.size());
    }
    ImGui::TextDisabled("%s", i18n::tr("Selection bounds, material and bone names are sent to "
                                       "Codex. The PMX file stays local."));
    ImGui::BeginDisabled(connection.busy || !connection.connected || state.prompt[0] == '\0' ||
                         state.preview != nullptr || session->hasPendingTransformEdit());
    if (ImGui::Button(i18n::tr("Generate preview"))) {
        try {
            // Freeze the current selection and revision at request time, never use later selection
            // changes.
            state.region = ai::captureRegion(*session);
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
    }
    if (state.preview && state.region) {
        ImGui::TextUnformatted(i18n::tr("Preview only. The original model has not changed."));
        const bool current = state.region->documentId == session->recoveryId &&
                             state.region->revision == session->revision;
        ImGui::BeginDisabled(!current);
        if (ImGui::Button(i18n::tr("Apply edit"))) {
            const auto result = ai::applyEditPlan(*session, *state.region, *state.plan);
            state.message =
                result.success ? i18n::tr("AI edit applied. Undo is available.") : result.message;
            if (result.success) {
                state.preview.reset();
                state.plan.reset();
                state.region.reset();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr("Discard preview"))) {
            state.preview.reset();
            state.plan.reset();
            state.region.reset();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled(
        "%s", i18n::tr("Procedural loafers, shirts and skirts; solid colors and transforms. Custom "
                       "fashion and generated textures are not supported."));
    ImGui::PopTextWrapPos();
    ImGui::End();
}
} // namespace pmxer
