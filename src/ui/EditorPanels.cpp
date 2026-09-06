#include "EditorPanels.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/EditorDiagnostics.hpp"
#include "../editor/DiffController.hpp"
#include "../editor/RecoveryController.hpp"
#include "../editor/SaveController.hpp"
#include "ViewportPanel.hpp"

#include <imgui.h>

namespace pmxer {

void drawEditorPanels(DocumentSession &session) {
    drawViewportPanel(session);
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("編集")) {
            if (ImGui::MenuItem("元に戻す", "Ctrl+Z", false, session.commands.undoCount() != 0))
                (void)session.undo();
            if (ImGui::MenuItem("やり直す", "Ctrl+Y", false, session.commands.redoCount() != 0))
                (void)session.redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("表示")) {
            ImGui::MenuItem("物理プレビュー", nullptr, &session.previewPhysics);
            ImGui::MenuItem("IKプレビュー", nullptr, &session.previewIk);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::Begin("モデル");
    ImGui::Text("頂点: %zu", session.document.model().vertices.size());
    ImGui::Text("材質: %zu", session.document.model().materials.size());
    ImGui::Text("ボーン: %zu", session.document.model().bones.size());
    ImGui::Text("モーフ: %zu", session.document.model().morphs.size());
    ImGui::Text("変更済み: %s", session.modified ? "はい" : "いいえ");
    ImGui::Text("Undo: %zu / Redo: %zu", session.commands.undoCount(), session.commands.redoCount());
    if (ImGui::Button("保存"))
        (void)saveDocument(session);
    ImGui::SameLine();
    if (ImGui::Button("回復保存"))
        (void)writeRecovery(session);
    const auto differences = compareWithBaseline(session);
    ImGui::Text("差分: %zu", differences.differences.size());
    ImGui::End();

    const auto detailed = validateForEditing(session.document.model());
    ImGui::Begin("診断");
    for (const auto &issue : detailed.issues)
        ImGui::TextWrapped("%s: %s", issue.object.c_str(), issue.message.c_str());
    ImGui::End();

    ImGui::Begin("参照");
    if (ImGui::BeginTabBar("entity-tabs")) {
        if (ImGui::BeginTabItem("頂点")) {
            ImGui::Text("%zu", session.document.model().vertices.size());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("材質")) {
            ImGui::Text("%zu", session.document.model().materials.size());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ボーン")) {
            ImGui::Text("%zu", session.document.model().bones.size());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("モーフ")) {
            ImGui::Text("%zu", session.document.model().morphs.size());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("物理")) {
            ImGui::Text("剛体 %zu / ジョイント %zu / SoftBody %zu", session.document.model().rigidBodies.size(),
                        session.document.model().joints.size(), session.document.model().softBodies.size());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace pmxer
