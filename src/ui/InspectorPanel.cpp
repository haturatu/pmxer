#include "InspectorPanel.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/ReferenceInspector.hpp"
#include "../preview/PreviewController.hpp"
#include "../render/GpuModelRenderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
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

template <typename Values>
bool referenceCombo(const char *label, std::int32_t &value,
                    const Values &values, bool optional) {
  const char *preview = "なし";
  if (value >= 0 && static_cast<std::size_t>(value) < values.size())
    preview = values[static_cast<std::size_t>(value)].name.c_str();
  bool changed{};
  if (ImGui::BeginCombo(label, preview)) {
    if (optional && ImGui::Selectable("なし", value < 0)) {
      value = -1;
      changed = true;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto selected = value >= 0 &&
                            static_cast<std::size_t>(value) == index;
      const auto &name = values[index].name;
      const auto itemLabel =
          (name.empty() ? std::string{"(名称なし)"} : name) +
          "##reference" + std::to_string(index);
      if (ImGui::Selectable(itemLabel.c_str(), selected)) {
        value = static_cast<std::int32_t>(index);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool enumCombo(const char *label, std::uint8_t &value,
               const char *const *labels, std::size_t count) {
  const auto current = std::min<std::size_t>(value, count - 1U);
  bool changed{};
  if (ImGui::BeginCombo(label, labels[current])) {
    for (std::size_t index = 0; index < count; ++index)
      if (ImGui::Selectable(labels[index], index == current)) {
        value = static_cast<std::uint8_t>(index);
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
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

std::string selectionSummary(const DocumentSession &session) {
  if (session.selection.items().empty())
    return "選択なし";
  const auto &selected = session.selection.items().front();
  std::string name;
  if (selected.kind == SelectionKind::material) {
    if (const auto *value = session.document.resolve(
            selectionHandle<mmd::MaterialTag>(session.document, selected)))
      name = value->name;
  } else if (selected.kind == SelectionKind::bone) {
    if (const auto *value = session.document.resolve(
            selectionHandle<mmd::BoneTag>(session.document, selected)))
      name = value->name;
  } else if (selected.kind == SelectionKind::morph) {
    if (const auto *value = session.document.resolve(
            selectionHandle<mmd::MorphTag>(session.document, selected)))
      name = value->name;
  } else if (selected.kind == SelectionKind::rigidBody) {
    if (const auto *value = session.document.resolve(
            selectionHandle<mmd::RigidBodyTag>(session.document, selected)))
      name = value->name;
  } else if (selected.kind == SelectionKind::joint) {
    if (const auto *value = session.document.resolve(
            selectionHandle<mmd::JointTag>(session.document, selected)))
      name = value->name;
  } else if (selected.kind == SelectionKind::texture) {
    if (const auto *value = session.document.resolve(
            selectionHandle<mmd::TextureTag>(session.document, selected)))
      name = value->storedPath;
  }
  auto result = std::string{kindName(selected.kind)};
  if (!name.empty())
    result += " \"" + name + "\"";
  if (session.selection.items().size() > 1U)
    result += " +" + std::to_string(session.selection.items().size() - 1U);
  return result;
}

void texturePreviewCard(const DocumentSession &session, const char *label,
                        std::int32_t index,
                        GpuModelRenderer *renderer) {
  if (index < 0 || renderer == nullptr)
    return;
  const auto preview =
      renderer->texturePreview(session, static_cast<std::size_t>(index));
  ImGui::TextUnformatted(label);
  if (preview.texture == nullptr || preview.width == 0U ||
      preview.height == 0U) {
    ImGui::TextDisabled("画像を読み込めません");
    return;
  }
  const auto maximum =
      std::max(1.0F, std::min(220.0F, ImGui::GetContentRegionAvail().x));
  const auto scale = std::min(maximum / static_cast<float>(preview.width),
                              maximum / static_cast<float>(preview.height));
  const ImVec2 size{static_cast<float>(preview.width) * scale,
                    static_cast<float>(preview.height) * scale};
  const auto identifier = static_cast<ImTextureID>(
      reinterpret_cast<std::uintptr_t>(preview.texture));
  ImGui::Image(ImTextureRef{identifier}, size);
  ImGui::TextDisabled("%u × %u", preview.width, preview.height);
}

void materialInspector(DocumentSession &session,
                       const SelectionItem &selected,
                       GpuModelRenderer *renderer) {
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
  texturePreviewCard(session, "基本テクスチャ", draft.textureIndex, renderer);
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
  if (draft.sphereMode != 0U)
    texturePreviewCard(session, "球面テクスチャ", draft.sphereTextureIndex,
                       renderer);
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
  if (draft.toonMode == 0U)
    texturePreviewCard(session, "個別トゥーン", draft.toonTextureIndex,
                       renderer);

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

void rigidBodyInspector(DocumentSession &session,
                        const SelectionItem &selected) {
  const auto handle =
      selectionHandle<mmd::RigidBodyTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.rigidBodyDraft)
    session.ui.rigidBodyDraft = *value;
  auto &draft = *session.ui.rigidBodyDraft;
  bool commit{};
  ImGui::SeparatorText("名前");
  (void)inputText("名前", draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText("英語名", draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::SeparatorText("関連付け");
  commit |= referenceCombo("ボーン", draft.bone,
                           session.document.model().bones, true);
  static constexpr const char *shapes[]{"球", "箱", "カプセル"};
  commit |= enumCombo("形状", draft.shape, shapes, std::size(shapes));
  static constexpr const char *modes[]{"ボーン追従", "物理演算",
                                        "物理演算 + ボーン"};
  commit |= enumCombo("動作", draft.mode, modes, std::size(modes));

  ImGui::SeparatorText("変形");
  (void)ImGui::DragFloat3("位置", draft.position.data(), 0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("回転", draft.rotation.data(), 0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("大きさ", draft.size.data(), 0.01F, 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::SeparatorText("衝突");
  int group = static_cast<int>(draft.group) + 1;
  if (ImGui::SliderInt("所属グループ", &group, 1, 16)) {
    draft.group = static_cast<std::uint8_t>(group - 1);
  }
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  if (ImGui::TreeNode("衝突除外グループ")) {
    for (std::size_t index = 0; index < 16U; ++index) {
      ImGui::PushID(static_cast<int>(index));
      const auto label = "グループ " + std::to_string(index + 1U);
      commit |= flagCheckbox(label.c_str(), draft.collisionMask,
                             static_cast<std::uint16_t>(1U << index));
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText("物理特性");
  (void)ImGui::DragFloat("質量", &draft.mass, 0.01F, 0.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat("移動減衰", &draft.linearDamping, 0.005F,
                         0.0F, 1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat("回転減衰", &draft.angularDamping, 0.005F,
                         0.0F, 1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat("反発", &draft.restitution, 0.005F, 0.0F,
                         1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat("摩擦", &draft.friction, 0.005F, 0.0F, 1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  if (commit) {
    const auto result = editRigidBody(session, handle, draft);
    session.ui.status = result.success ? "剛体を更新しました" : result.message;
    session.ui.rigidBodyDraft.reset();
  }
}

void jointInspector(DocumentSession &session, const SelectionItem &selected) {
  const auto handle =
      selectionHandle<mmd::JointTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.jointDraft)
    session.ui.jointDraft = *value;
  auto &draft = *session.ui.jointDraft;
  bool commit{};
  ImGui::SeparatorText("名前");
  (void)inputText("名前", draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText("英語名", draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  static constexpr const char *types[]{"ばね付き6軸", "6軸", "点接続",
                                       "円錐ねじり", "スライダー", "ヒンジ"};
  commit |= enumCombo("種類", draft.type, types, std::size(types));
  if (draft.type != 0U)
    ImGui::TextDisabled("この種類は編集・保存できますが、プレビュー未対応です");

  ImGui::SeparatorText("接続");
  commit |= referenceCombo("剛体A", draft.bodyA,
                           session.document.model().rigidBodies, false);
  commit |= referenceCombo("剛体B", draft.bodyB,
                           session.document.model().rigidBodies, false);
  ImGui::SeparatorText("変形");
  (void)ImGui::DragFloat3("位置", draft.position.data(), 0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("回転", draft.rotation.data(), 0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText("移動制限");
  (void)ImGui::DragFloat3("最小##translation", draft.translationMinimum.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("最大##translation", draft.translationMaximum.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("ばね##translation", draft.translationSpring.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText("回転制限");
  (void)ImGui::DragFloat3("最小##rotation", draft.rotationMinimum.data(),
                          0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("最大##rotation", draft.rotationMaximum.data(),
                          0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3("ばね##rotation", draft.rotationSpring.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  if (commit) {
    const auto result = editJoint(session, handle, draft);
    session.ui.status =
        result.success ? "ジョイントを更新しました" : result.message;
    session.ui.jointDraft.reset();
  }
}

void refreshPreviewFrame(DocumentSession &session) {
  auto &preview = session.preview;
  if (!preview.controller)
    return;
  preview.frame = preview.controller->evaluate();
  ++preview.frameRevision;
  session.ui.previewFrame = &*preview.frame;
}

void morphInspector(DocumentSession &session, const SelectionItem &selected) {
  const auto handle = selectionHandle<mmd::MorphTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.morphDraft)
    session.ui.morphDraft = *value;
  auto &draft = *session.ui.morphDraft;
  bool commit{};

  ImGui::SeparatorText("名前");
  (void)inputText("名前", draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText("英語名", draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  static constexpr const char *panels[]{"予約", "まゆ", "目", "リップ", "その他"};
  commit |= enumCombo("表示区分", draft.panel, panels, std::size(panels));
  static constexpr const char *types[]{"グループ", "頂点", "ボーン", "UV", "追加UV1", "追加UV2",
                                       "追加UV3", "追加UV4", "材質", "フリップ", "インパルス"};
  if (draft.offsets.empty()) {
    commit |= enumCombo("種類", draft.type, types, std::size(types));
  } else {
    const auto type = std::min<std::size_t>(draft.type, std::size(types) - 1U);
    ImGui::Text("種類: %s", types[type]);
    ImGui::TextDisabled("オフセットがあるモーフの種類は変更できません");
  }
  ImGui::Text("オフセット: %zu", draft.offsets.size());

  ImGui::SeparatorText("プレビュー");
  auto &values = session.preview.morphValues;
  auto preview = std::find_if(values.begin(), values.end(),
                              [&](const auto &item) { return item.selection == selected; });
  float weight = preview == values.end() ? 0.0F : preview->weight;
  if (ImGui::SliderFloat("ウェイト", &weight, 0.0F, 1.0F, "%.2f")) {
    if (preview == values.end())
      values.push_back({selected, weight});
    else
      preview->weight = weight;
    if (session.preview.controller) {
      session.preview.controller->setMorphPreview(value->name, weight);
      refreshPreviewFrame(session);
    }
  }
  if (ImGui::Button("このモーフをリセット")) {
    preview = std::find_if(values.begin(), values.end(),
                           [&](const auto &item) { return item.selection == selected; });
    if (preview != values.end())
      values.erase(preview);
    if (session.preview.controller) {
      session.preview.controller->clearMorphPreview(value->name);
      refreshPreviewFrame(session);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("すべてリセット")) {
    values.clear();
    if (session.preview.controller) {
      session.preview.controller->clearMorphPreviews();
      refreshPreviewFrame(session);
    }
  }
  if (!values.empty())
    ImGui::TextDisabled("%zu個のモーフを同時プレビュー中", values.size());

  if (commit) {
    const auto result = editMorph(session, handle, draft);
    session.ui.status = result.success ? "モーフを更新しました" : result.message;
    session.ui.morphDraft.reset();
  }
}

void readOnlyInspector(DocumentSession &session,
                       const SelectionItem &selected,
                       GpuModelRenderer *renderer) {
  const auto &model = session.document.model();
  if (selected.kind == SelectionKind::texture) {
    const auto *texture = session.document.resolve(
        selectionHandle<mmd::TextureTag>(session.document, selected));
    if (texture != nullptr) {
      const auto handle =
          selectionHandle<mmd::TextureTag>(session.document, selected);
      std::optional<std::size_t> textureIndex;
      for (std::size_t index = 0; index < model.textures.size(); ++index) {
        if (session.document.textureHandle(index) ==
            selectionHandle<mmd::TextureTag>(session.document, selected)) {
          textureIndex = index;
          texturePreviewCard(session, "プレビュー",
                             static_cast<std::int32_t>(index), renderer);
          break;
        }
      }
      if (!session.ui.textureDraft)
        session.ui.textureDraft = *texture;
      auto &draft = *session.ui.textureDraft;
      ImGui::SeparatorText("パス");
      (void)inputText("保存パス", draft.storedPath);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const auto result = editTexture(session, handle, draft);
        session.ui.status =
            result.success ? "テクスチャを更新しました" : result.message;
        session.ui.textureDraft.reset();
        return;
      }
      const auto path = textureIndex
                            ? mmd::pmx::resolveTexturePath(model, *textureIndex)
                            : std::filesystem::path{};
      ImGui::TextWrapped("%s", path.string().c_str());
      ImGui::TextUnformatted(!path.empty() && std::filesystem::exists(path)
                                 ? "✓ 読み込み可能"
                                 : "⚠ ファイルがありません");
    }
  }
}

} // namespace

void drawInspectorPanel(DocumentSession &session, GpuModelRenderer *renderer,
                        EditorWorkspace activeWorkspace, bool *open) {
  if (!ImGui::Begin("インスペクター", open)) {
    ImGui::End();
    return;
  }
  if (session.selection.items().empty()) {
    switch (activeWorkspace) {
    case EditorWorkspace::model:
        ImGui::TextUnformatted("モデル");
        ImGui::TextUnformatted("材質またはテクスチャを");
        ImGui::TextUnformatted("アウトライナーから選択してください");
        break;
    case EditorWorkspace::rig:
        ImGui::TextUnformatted("リグ");
        ImGui::TextUnformatted("頂点またはボーンを選択してください");
        break;
    case EditorWorkspace::morph:
        ImGui::TextUnformatted("モーフ");
        ImGui::TextUnformatted("モーフをアウトライナーから選択してください");
        break;
    case EditorWorkspace::physics:
        ImGui::TextUnformatted("物理");
        ImGui::TextUnformatted("剛体、ジョイント、ソフトボディを選択してください");
        break;
    case EditorWorkspace::inspect:
        ImGui::TextUnformatted("Outlinerまたはビューポートで対象を選択してください");
        break;
    }
    ImGui::End();
    return;
  }
  const auto selected = session.selection.items().front();
  if (!workspacePolicy(activeWorkspace).allows(selected.kind)) {
    ImGui::TextDisabled("このワークスペースでは選択対象を編集できません");
    ImGui::End();
    return;
  }
  ImGui::Text("%s", kindName(selected.kind));
  if (session.selection.items().size() > 1U) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu件選択)", session.selection.items().size());
  }
  switch (selected.kind) {
  case SelectionKind::material:
    materialInspector(session, selected, renderer);
    break;
  case SelectionKind::bone:
    boneInspector(session, selected);
    break;
  case SelectionKind::vertex:
    vertexInspector(session, selected);
    break;
  case SelectionKind::rigidBody:
    rigidBodyInspector(session, selected);
    break;
  case SelectionKind::joint:
    jointInspector(session, selected);
    break;
  case SelectionKind::morph:
    morphInspector(session, selected);
    break;
  default:
    readOnlyInspector(session, selected, renderer);
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
    const auto selected = selectionSummary(session);
    if (ImGui::SmallButton(selected.c_str()))
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
