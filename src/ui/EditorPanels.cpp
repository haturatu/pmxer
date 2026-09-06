#include "EditorPanels.hpp"

#include "../editor/DocumentSession.hpp"

#include <imgui.h>

namespace pmxer {

void drawEditorPanels(DocumentSession &session) {
    ImGui::Begin("モデル");
    ImGui::Text("頂点: %zu", session.document.model().vertices.size());
    ImGui::Text("材質: %zu", session.document.model().materials.size());
    ImGui::Text("ボーン: %zu", session.document.model().bones.size());
    ImGui::Text("モーフ: %zu", session.document.model().morphs.size());
    ImGui::Text("変更済み: %s", session.modified ? "はい" : "いいえ");
    ImGui::End();

    ImGui::Begin("診断");
    for (const auto &issue : session.validation.issues)
        ImGui::TextWrapped("%s: %s", issue.object.c_str(), issue.message.c_str());
    ImGui::End();
}

} // namespace pmxer

