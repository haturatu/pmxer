#include "InspectorPanel.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/ReferenceInspector.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace pmxer {
namespace {

int resizeText(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
    return 0;
  auto &value = *static_cast<std::string *>(data->UserData);
  value.resize(static_cast<std::size_t>(data->BufTextLen));
  data->Buf = value.data();
  return 0;
}

bool inputText(const char *label, std::string &value) {
  if (value.capacity() < value.size() + 1U)
    value.reserve(value.size() + 1U);
  const auto changed =
      ImGui::InputText(label, value.data(), value.capacity() + 1U,
                       ImGuiInputTextFlags_CallbackResize, resizeText, &value);
  value.resize(std::strlen(value.c_str()));
  return changed;
}

bool flagCheckbox(const char *label, std::uint8_t &flags, std::uint8_t bit) {
  bool enabled = (flags & bit) != 0U;
  if (!ImGui::Checkbox(label, &enabled))
    return false;
  flags =
      enabled
          ? static_cast<std::uint8_t>(flags | bit)
          : static_cast<std::uint8_t>(flags & static_cast<std::uint8_t>(~bit));
  return true;
}

bool flagCheckbox(const char *label, std::uint16_t &flags, std::uint16_t bit) {
  bool enabled = (flags & bit) != 0U;
  if (!ImGui::Checkbox(label, &enabled))
    return false;
  flags = enabled ? static_cast<std::uint16_t>(flags | bit)
                  : static_cast<std::uint16_t>(
                        flags & static_cast<std::uint16_t>(~bit));
  return true;
}

const char *kindName(SelectionKind kind) {
  switch (kind) {
  case SelectionKind::vertex:
    return "頂点";
  case SelectionKind::texture:
    return "テクスチャ";
  case SelectionKind::material:
    return "材質";
  case SelectionKind::bone:
    return "ボーン";
  case SelectionKind::morph:
    return "モーフ";
  case SelectionKind::displayFrame:
    return "表示枠";
  case SelectionKind::rigidBody:
    return "剛体";
  case SelectionKind::joint:
    return "ジョイント";
  case SelectionKind::softBody:
    return "ソフトボディ";
  case SelectionKind::face:
    return "面";
  }
  return "選択";
}

void materialInspector(DocumentSession &session,
                       const SelectionItem &selected) {
  const auto handle =
      selectionHandle<mmd::MaterialTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.materialDraft)
    session.ui.materialDraft = *value;
  auto &draft = *session.ui.materialDraft;
  bool commit{};
  ImGui::SeparatorText("名前");
  (void)inputText("名前", draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText("英語名", draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText("表面");
  (void)ImGui::ColorEdit4("拡散色", draft.diffuse.data());
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::ColorEdit3("環境色", draft.ambient.data());
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::ColorEdit3("鏡面色", draft.specular.data());
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat("光沢", &draft.shininess, 0.1F, 0.0F, 1000.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::SeparatorText("テクスチャ");
  static constexpr const char *sphereModes[]{"なし", "乗算", "加算",
                                             "サブテクスチャ"};
  const auto sphere = std::min<std::size_t>(draft.sphereMode, 3U);
  if (ImGui::BeginCombo("球面方式", sphereModes[sphere])) {
    for (std::size_t index = 0; index < 4U; ++index)
      if (ImGui::Selectable(sphereModes[index], index == sphere)) {
        draft.sphereMode = static_cast<std::uint8_t>(index);
        commit = true;
      }
    ImGui::EndCombo();
  }
  const char *toonModes[]{"共有", "個別"};
  const auto toon = std::min<std::size_t>(draft.toonMode, 1U);
  if (ImGui::BeginCombo("トゥーン", toonModes[toon])) {
    for (std::size_t index = 0; index < 2U; ++index)
      if (ImGui::Selectable(toonModes[index], index == toon)) {
        draft.toonMode = static_cast<std::uint8_t>(index);
        commit = true;
      }
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("描画");
  commit |= flagCheckbox("両面", draft.drawFlags, std::uint8_t{0x01U});
  commit |= flagCheckbox("地面影", draft.drawFlags, std::uint8_t{0x02U});
  commit |= flagCheckbox("セルフシャドウマップ", draft.drawFlags,
                         std::uint8_t{0x04U});
  commit |=
      flagCheckbox("セルフシャドウ", draft.drawFlags, std::uint8_t{0x08U});
  commit |= flagCheckbox("輪郭", draft.drawFlags, std::uint8_t{0x10U});
  if ((draft.drawFlags & 0x10U) != 0U) {
    (void)ImGui::ColorEdit4("輪郭色", draft.edgeColor.data());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::DragFloat("輪郭幅", &draft.edgeSize, 0.01F, 0.0F, 100.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
  }
  if (commit) {
    const auto result = editMaterial(session, handle, draft);
    session.ui.status = result.success ? "材質を更新しました" : result.message;
    session.ui.materialDraft.reset();
  }
}

void boneInspector(DocumentSession &session, const SelectionItem &selected) {
  const auto handle = selectionHandle<mmd::BoneTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.boneDraft)
    session.ui.boneDraft = *value;
  auto &draft = *session.ui.boneDraft;
  bool commit{};
  ImGui::SeparatorText("名前");
  (void)inputText("名前", draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText("英語名", draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText("変形");
  (void)ImGui::DragFloat3("位置", draft.position.data(), 0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::InputInt("変形層", &draft.deformLayer);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText("機能");
  commit |= flagCheckbox("回転可能", draft.flags, std::uint16_t{0x0002U});
  commit |= flagCheckbox("移動可能", draft.flags, std::uint16_t{0x0004U});
  commit |= flagCheckbox("表示", draft.flags, std::uint16_t{0x0008U});
  commit |= flagCheckbox("操作可能", draft.flags, std::uint16_t{0x0010U});
  commit |= flagCheckbox("IK", draft.flags, std::uint16_t{0x0020U});
  commit |= flagCheckbox("回転付与", draft.flags, std::uint16_t{0x0100U});
  commit |= flagCheckbox("移動付与", draft.flags, std::uint16_t{0x0200U});
  commit |= flagCheckbox("軸固定", draft.flags, std::uint16_t{0x0400U});
  commit |= flagCheckbox("ローカル軸", draft.flags, std::uint16_t{0x0800U});
  commit |= flagCheckbox("物理後変形", draft.flags, std::uint16_t{0x1000U});
  if ((draft.flags & 0x0020U) != 0U) {
    ImGui::SeparatorText("IK");
    (void)ImGui::InputInt("反復回数", &draft.ikLoopCount);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::DragFloat("角度制限", &draft.ikLimitAngle, 0.001F, 0.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Text("リンク: %zu", draft.ikLinks.size());
  }
  if (commit) {
    const auto result = editBone(session, handle, draft);
    session.ui.status =
        result.success ? "ボーンを更新しました" : result.message;
    session.ui.boneDraft.reset();
  }
}

void vertexInspector(DocumentSession &session, const SelectionItem &selected) {
  const auto handle =
      selectionHandle<mmd::VertexTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.vertexDraft)
    session.ui.vertexDraft = *value;
  auto &draft = *session.ui.vertexDraft;
  bool commit{};
  (void)ImGui::DragFloat3("位置", draft.position.data(), 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("法線", draft.normal.data(), 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat2("UV", draft.uv.data(), 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat("輪郭倍率", &draft.edgeScale, 0.01F, 0.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText("ウェイト");
  for (std::size_t index = 0; index < draft.weights.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    (void)ImGui::DragFloat("重み", &draft.weights[index], 0.005F, 0.0F, 1.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (draft.bones[index] >= 0 &&
        static_cast<std::size_t>(draft.bones[index]) <
            session.document.model().bones.size()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("→")) {
        const auto boneIndex = static_cast<std::size_t>(draft.bones[index]);
        const auto bone = session.document.boneHandle(boneIndex);
        session.selection.set(
            {SelectionKind::bone, bone.domain, bone.id, bone.generation});
        session.ui.boneIndex = boneIndex;
        session.ui.clearDrafts();
        ImGui::PopID();
        return;
      }
    }
    ImGui::PopID();
  }
  if (commit) {
    const auto result = editVertex(session, handle, draft);
    session.ui.status = result.success ? "頂点を更新しました" : result.message;
    session.ui.vertexDraft.reset();
  }
}

void readOnlyInspector(DocumentSession &session,
                       const SelectionItem &selected) {
  const auto &model = session.document.model();
  if (selected.kind == SelectionKind::texture) {
    const auto *texture = session.document.resolve(
        selectionHandle<mmd::TextureTag>(session.document, selected));
    if (texture != nullptr) {
      ImGui::TextWrapped("%s", texture->storedPath.c_str());
      const auto path = (model.sourcePath.parent_path() / texture->storedPath)
                            .lexically_normal();
      ImGui::TextWrapped("%s", path.string().c_str());
      ImGui::TextUnformatted(std::filesystem::exists(path)
                                 ? "✓ 読み込み可能"
                                 : "⚠ ファイルがありません");
    }
  } else if (selected.kind == SelectionKind::morph) {
    const auto *morph = session.document.resolve(
        selectionHandle<mmd::MorphTag>(session.document, selected));
    if (morph != nullptr) {
      ImGui::Text("%s", morph->name.c_str());
      ImGui::Text("オフセット: %zu", morph->offsets.size());
    }
  }
}

} // namespace

void drawInspectorPanel(DocumentSession &session, bool *open) {
  if (!ImGui::Begin("インスペクター", open)) {
    ImGui::End();
    return;
  }
  if (session.selection.items().empty()) {
    ImGui::TextUnformatted(
        "Outlinerまたはビューポートで対象を選択してください");
    ImGui::End();
    return;
  }
  const auto selected = session.selection.items().front();
  ImGui::Text("%s", kindName(selected.kind));
  if (session.selection.items().size() > 1U) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu件選択)", session.selection.items().size());
  }
  switch (selected.kind) {
  case SelectionKind::material:
    materialInspector(session, selected);
    break;
  case SelectionKind::bone:
    boneInspector(session, selected);
    break;
  case SelectionKind::vertex:
    vertexInspector(session, selected);
    break;
  default:
    readOnlyInspector(session, selected);
    break;
  }
  ImGui::End();
}

void drawStatusBar(DocumentSession &session, bool &showDiagnostics,
                   bool &showReferences, bool &showDiff) {
  const auto *viewport = ImGui::GetMainViewport();
  const auto height =
      ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0F;
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x,
       viewport->WorkPos.y + viewport->WorkSize.y - height});
  ImGui::SetNextWindowSize({viewport->WorkSize.x, height});
  constexpr auto flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
  if (ImGui::Begin("##status-bar", nullptr, flags)) {
    ImGui::TextUnformatted(session.modified ? "● 未保存" : "✓ 保存済み");
    ImGui::SameLine();
    if (ImGui::SmallButton(
            ("診断 " + std::to_string(session.validation.issues.size()))
                .c_str()))
      showDiagnostics = true;
    ImGui::SameLine();
    if (ImGui::SmallButton(
            ("選択 " + std::to_string(session.selection.items().size()))
                .c_str()))
      showReferences = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("差分"))
      showDiff = true;
    if (!session.ui.status.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", session.ui.status.c_str());
    }
  }
  ImGui::End();
}

} // namespace pmxer
