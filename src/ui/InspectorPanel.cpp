#include "i18n/Language.hpp"

#include "InspectorPanel.hpp"
#include "MorphOffsetBrowser.hpp"

#include "../editor/EditorSelectionController.hpp"

#include "EditorPanels.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/ReferenceInspector.hpp"
#include "../editor/UiStatus.hpp"
#include "../editor/tools/MorphTool.hpp"
#include "../editor/morph/MorphMixer.hpp"
#include "../preview/PreviewController.hpp"
#include "../render/GpuModelRenderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

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
  const char *preview = i18n::tr("なし");
  if (value >= 0 && static_cast<std::size_t>(value) < values.size())
    preview = values[static_cast<std::size_t>(value)].name.c_str();
  bool changed{};
  if (ImGui::BeginCombo(label, preview)) {
    if (optional && ImGui::Selectable(i18n::tr("なし"), value < 0)) {
      value = -1;
      changed = true;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto selected = value >= 0 &&
                            static_cast<std::size_t>(value) == index;
      const auto &name = values[index].name;
      const auto itemLabel =
          (name.empty() ? std::string{i18n::tr("(名称なし)")} : name) +
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
    return i18n::tr("頂点");
  case SelectionKind::texture:
    return i18n::tr("テクスチャ");
  case SelectionKind::material:
    return i18n::tr("材質");
  case SelectionKind::bone:
    return i18n::tr("ボーン");
  case SelectionKind::morph:
    return i18n::tr("モーフ");
  case SelectionKind::displayFrame:
    return i18n::tr("表示枠");
  case SelectionKind::rigidBody:
    return i18n::tr("剛体");
  case SelectionKind::joint:
    return i18n::tr("ジョイント");
  case SelectionKind::softBody:
    return i18n::tr("ソフトボディ");
  case SelectionKind::face:
    return i18n::tr("面");
  }
  return i18n::tr("選択");
}

std::string selectionSummary(const DocumentSession &session) {
  if (session.selection.items().empty())
    return i18n::tr("選択なし");
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
    ImGui::TextDisabled("%s", i18n::tr("画像を読み込めません"));
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
  const auto compact = ImGui::GetContentRegionAvail().x < 380.0F;
  if (ImGui::CollapsingHeader(i18n::tr("基本"), ImGuiTreeNodeFlags_DefaultOpen)) {
    (void)inputText(i18n::tr("名前"), draft.name);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)inputText(i18n::tr("英語名"), draft.englishName);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SeparatorText(i18n::tr("表面"));
    (void)ImGui::ColorEdit4(i18n::tr("拡散色"), draft.diffuse.data());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::ColorEdit3(i18n::tr("環境色"), draft.ambient.data());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::ColorEdit3(i18n::tr("鏡面色"), draft.specular.data());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::DragFloat(i18n::tr("光沢"), &draft.shininess, 0.1F, 0.0F, 1000.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
  }

  if (ImGui::CollapsingHeader(i18n::tr("テクスチャ"), ImGuiTreeNodeFlags_DefaultOpen)) {
    texturePreviewCard(session, i18n::tr("基本テクスチャ"), draft.textureIndex, renderer);
  const char *sphereModes[]{i18n::tr("なし"), i18n::tr("乗算"), i18n::tr("加算"),
                                             i18n::tr("サブテクスチャ")};
  const auto sphere = std::min<std::size_t>(draft.sphereMode, 3U);
  if (ImGui::BeginCombo(i18n::tr("球面方式"), sphereModes[sphere])) {
    for (std::size_t index = 0; index < 4U; ++index)
      if (ImGui::Selectable(sphereModes[index], index == sphere)) {
        draft.sphereMode = static_cast<std::uint8_t>(index);
        commit = true;
      }
    ImGui::EndCombo();
  }
  if (draft.sphereMode != 0U)
    texturePreviewCard(session, i18n::tr("球面テクスチャ"), draft.sphereTextureIndex,
                       renderer);
  const char *toonModes[]{i18n::tr("個別テクスチャ"), i18n::tr("共有トゥーン")};
  const auto toon = std::min<std::size_t>(draft.toonMode, 1U);
  if (ImGui::BeginCombo(i18n::tr("トゥーン"), toonModes[toon])) {
    for (std::size_t index = 0; index < 2U; ++index)
      if (ImGui::Selectable(toonModes[index], index == toon)) {
        draft.toonMode = static_cast<std::uint8_t>(index);
        commit = true;
      }
    ImGui::EndCombo();
  }
  if (draft.toonMode == 0U)
    texturePreviewCard(session, i18n::tr("個別トゥーン"), draft.toonTextureIndex,
                       renderer);
  }

  if (ImGui::CollapsingHeader(i18n::tr("詳細描画"), compact ? 0 : ImGuiTreeNodeFlags_None)) {
  commit |= flagCheckbox(i18n::tr("両面"), draft.drawFlags, std::uint8_t{0x01U});
  commit |= flagCheckbox(i18n::tr("地面影"), draft.drawFlags, std::uint8_t{0x02U});
  commit |= flagCheckbox(i18n::tr("セルフシャドウマップ"), draft.drawFlags,
                         std::uint8_t{0x04U});
  commit |=
      flagCheckbox(i18n::tr("セルフシャドウ"), draft.drawFlags, std::uint8_t{0x08U});
  commit |= flagCheckbox(i18n::tr("輪郭"), draft.drawFlags, std::uint8_t{0x10U});
  if ((draft.drawFlags & 0x10U) != 0U) {
    (void)ImGui::ColorEdit4(i18n::tr("輪郭色"), draft.edgeColor.data());
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::DragFloat(i18n::tr("輪郭幅"), &draft.edgeSize, 0.01F, 0.0F, 100.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
  }
  }
  if (commit) {
    const auto result = editMaterial(session, handle, draft);
    setOperationStatus(session, result.success, i18n::tr("材質を更新しました"),
                       result.message);
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
  ImGui::SeparatorText(i18n::tr("名前"));
  (void)inputText(i18n::tr("名前"), draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText(i18n::tr("英語名"), draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText(i18n::tr("変形"));
  (void)ImGui::DragFloat3(i18n::tr("位置"), draft.position.data(), 0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::InputInt(i18n::tr("変形層"), &draft.deformLayer);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText(i18n::tr("機能"));
  commit |= flagCheckbox(i18n::tr("回転可能"), draft.flags, std::uint16_t{0x0002U});
  commit |= flagCheckbox(i18n::tr("移動可能"), draft.flags, std::uint16_t{0x0004U});
  commit |= flagCheckbox(i18n::tr("表示"), draft.flags, std::uint16_t{0x0008U});
  commit |= flagCheckbox(i18n::tr("操作可能"), draft.flags, std::uint16_t{0x0010U});
  commit |= flagCheckbox("IK", draft.flags, std::uint16_t{0x0020U});
  commit |= flagCheckbox(i18n::tr("回転付与"), draft.flags, std::uint16_t{0x0100U});
  commit |= flagCheckbox(i18n::tr("移動付与"), draft.flags, std::uint16_t{0x0200U});
  commit |= flagCheckbox(i18n::tr("軸固定"), draft.flags, std::uint16_t{0x0400U});
  commit |= flagCheckbox(i18n::tr("ローカル軸"), draft.flags, std::uint16_t{0x0800U});
  commit |= flagCheckbox(i18n::tr("物理後変形"), draft.flags, std::uint16_t{0x1000U});
  if ((draft.flags & 0x0020U) != 0U) {
    ImGui::SeparatorText("IK");
    (void)ImGui::InputInt(i18n::tr("反復回数"), &draft.ikLoopCount);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    (void)ImGui::DragFloat(i18n::tr("角度制限"), &draft.ikLimitAngle, 0.001F, 0.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Text(i18n::tr("リンク: %zu"), draft.ikLinks.size());
  }
  if (commit) {
    const auto result = editBone(session, handle, draft);
    setOperationStatus(session, result.success, i18n::tr("ボーンを更新しました"),
                       result.message);
    session.ui.boneDraft.reset();
  }
}

void vertexInspector(DocumentSession &session, EditorWorkspace &workspace,
                     const SelectionItem &selected) {
  const auto handle =
      selectionHandle<mmd::VertexTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.vertexDraft)
    session.ui.vertexDraft = *value;
  auto &draft = *session.ui.vertexDraft;
  bool commit{};
  (void)ImGui::DragFloat3(i18n::tr("位置"), draft.position.data(), 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("法線"), draft.normal.data(), 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat2("UV", draft.uv.data(), 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat(i18n::tr("輪郭倍率"), &draft.edgeScale, 0.01F, 0.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText(i18n::tr("ウェイト"));
  for (std::size_t index = 0; index < draft.weights.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    (void)ImGui::DragFloat(i18n::tr("重み"), &draft.weights[index], 0.005F, 0.0F, 1.0F);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (draft.bones[index] >= 0 &&
        static_cast<std::size_t>(draft.bones[index]) <
            session.document.model().bones.size()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("→")) {
        const auto boneIndex = static_cast<std::size_t>(draft.bones[index]);
        const auto bone = session.document.boneHandle(boneIndex);
        selectPrimary(session, workspace,
                      {SelectionKind::bone, bone.domain, bone.id,
                       bone.generation},
                      SelectionOrigin::inspectorLink);
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
    setOperationStatus(session, result.success, i18n::tr("頂点を更新しました"),
                       result.message);
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
  ImGui::SeparatorText(i18n::tr("名前"));
  (void)inputText(i18n::tr("名前"), draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText(i18n::tr("英語名"), draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::SeparatorText(i18n::tr("関連付け"));
  commit |= referenceCombo(i18n::tr("ボーン"), draft.bone,
                           session.document.model().bones, true);
  const char *shapes[]{i18n::tr("球"), i18n::tr("箱"), i18n::tr("カプセル")};
  commit |= enumCombo(i18n::tr("形状"), draft.shape, shapes, std::size(shapes));
  const char *modes[]{i18n::tr("ボーン追従"), i18n::tr("物理演算"),
                                        i18n::tr("物理演算 + ボーン")};
  commit |= enumCombo(i18n::tr("動作"), draft.mode, modes, std::size(modes));

  ImGui::SeparatorText(i18n::tr("変形"));
  (void)ImGui::DragFloat3(i18n::tr("位置"), draft.position.data(), 0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("回転"), draft.rotation.data(), 0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("大きさ"), draft.size.data(), 0.01F, 0.001F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::SeparatorText(i18n::tr("衝突"));
  int group = static_cast<int>(draft.group) + 1;
  if (ImGui::SliderInt(i18n::tr("所属グループ"), &group, 1, 16)) {
    draft.group = static_cast<std::uint8_t>(group - 1);
  }
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  if (ImGui::TreeNode(i18n::tr("衝突除外グループ"))) {
    for (std::size_t index = 0; index < 16U; ++index) {
      ImGui::PushID(static_cast<int>(index));
      const auto label = i18n::tr("グループ ") + std::to_string(index + 1U);
      commit |= flagCheckbox(label.c_str(), draft.collisionMask,
                             static_cast<std::uint16_t>(1U << index));
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  ImGui::SeparatorText(i18n::tr("物理特性"));
  (void)ImGui::DragFloat(i18n::tr("質量"), &draft.mass, 0.01F, 0.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat(i18n::tr("移動減衰"), &draft.linearDamping, 0.005F,
                         0.0F, 1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat(i18n::tr("回転減衰"), &draft.angularDamping, 0.005F,
                         0.0F, 1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat(i18n::tr("反発"), &draft.restitution, 0.005F, 0.0F,
                         1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat(i18n::tr("摩擦"), &draft.friction, 0.005F, 0.0F, 1.0F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  if (commit) {
    const auto result = editRigidBody(session, handle, draft);
    setOperationStatus(session, result.success, i18n::tr("剛体を更新しました"),
                       result.message);
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
  ImGui::SeparatorText(i18n::tr("名前"));
  (void)inputText(i18n::tr("名前"), draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText(i18n::tr("英語名"), draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  const char *types[]{i18n::tr("ばね付き6軸"), i18n::tr("6軸"), i18n::tr("点接続"),
                                       i18n::tr("円錐ねじり"), i18n::tr("スライダー"), i18n::tr("ヒンジ")};
  commit |= enumCombo(i18n::tr("種類"), draft.type, types, std::size(types));
  if (draft.type != 0U)
    ImGui::TextDisabled("%s", i18n::tr("この種類は編集・保存できますが、プレビュー未対応です"));

  ImGui::SeparatorText(i18n::tr("接続"));
  commit |= referenceCombo(i18n::tr("剛体A"), draft.bodyA,
                           session.document.model().rigidBodies, false);
  commit |= referenceCombo(i18n::tr("剛体B"), draft.bodyB,
                           session.document.model().rigidBodies, false);
  ImGui::SeparatorText(i18n::tr("変形"));
  (void)ImGui::DragFloat3(i18n::tr("位置"), draft.position.data(), 0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("回転"), draft.rotation.data(), 0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText(i18n::tr("移動制限"));
  (void)ImGui::DragFloat3(i18n::tr("最小##translation"), draft.translationMinimum.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("最大##translation"), draft.translationMaximum.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("ばね##translation"), draft.translationSpring.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SeparatorText(i18n::tr("回転制限"));
  (void)ImGui::DragFloat3(i18n::tr("最小##rotation"), draft.rotationMinimum.data(),
                          0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("最大##rotation"), draft.rotationMaximum.data(),
                          0.005F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)ImGui::DragFloat3(i18n::tr("ばね##rotation"), draft.rotationSpring.data(),
                          0.01F);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  if (commit) {
    const auto result = editJoint(session, handle, draft);
    setOperationStatus(session, result.success, i18n::tr("ジョイントを更新しました"),
                       result.message);
    session.ui.jointDraft.reset();
  }
}

SelectionKind morphOffsetTargetKind(std::uint8_t type) noexcept {
  switch (type) {
  case 0:
  case 9:
    return SelectionKind::morph;
  case 2:
    return SelectionKind::bone;
  case 8:
    return SelectionKind::material;
  case 10:
    return SelectionKind::rigidBody;
  case 1:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
  default:
    return SelectionKind::vertex;
  }
}

bool morphIndexCombo(const char *label, DocumentSession &session,
                     std::int32_t &index, mmd::MorphHandle excluded) {
  const auto &model = session.document.model();
  constexpr auto noSelection = std::numeric_limits<std::size_t>::max();
  const auto current = index >= 0 && static_cast<std::size_t>(index) < model.morphs.size() &&
                               session.document.morphHandle(static_cast<std::size_t>(index)) != excluded
                           ? static_cast<std::size_t>(index)
                           : noSelection;
  const auto preview = current == noSelection
                           ? i18n::tr("未選択")
                           : model.morphs[current].name.c_str();
  bool changed{};
  if (ImGui::BeginCombo(label, preview)) {
    if (ImGui::Selectable(i18n::tr("未選択"), current == noSelection)) {
      index = -1;
      changed = true;
    }
    for (std::size_t candidate = 0; candidate < model.morphs.size(); ++candidate) {
      const auto handle = session.document.morphHandle(candidate);
      if (handle == excluded)
        continue;
      const auto selected = current == candidate;
      const auto itemLabel = (model.morphs[candidate].name.empty()
                                  ? std::string{i18n::tr("(名称なし)")}
                                  : model.morphs[candidate].name) +
                             "##morph-target" + std::to_string(candidate);
      if (ImGui::Selectable(itemLabel.c_str(), selected)) {
        index = static_cast<std::int32_t>(candidate);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

[[maybe_unused]] std::string indexedOffsetName(std::string_view name,
                                               std::size_t index) {
  return name.empty() ? "#" + std::to_string(index) : std::string(name);
}

[[maybe_unused]] std::string morphOffsetTargetLabel(const DocumentSession &session,
                                   const mmd::PmxMorph &morph,
                                   std::size_t offsetIndex) {
  const auto &offset = morph.offsets[offsetIndex];
  if (morph.type == 0U || morph.type == 9U) {
    if (offset.index >= 0 &&
        static_cast<std::size_t>(offset.index) < session.document.model().morphs.size())
      return i18n::tr("モーフ ") + indexedOffsetName(session.document.model().morphs[
                                                  static_cast<std::size_t>(offset.index)].name,
                                              static_cast<std::size_t>(offset.index));
    return i18n::tr("モーフ ") + std::to_string(offset.index);
  }
  if (morph.type == 2U && offset.index >= 0 &&
      static_cast<std::size_t>(offset.index) < session.document.model().bones.size())
    return i18n::tr("ボーン ") + indexedOffsetName(session.document.model().bones[
                                               static_cast<std::size_t>(offset.index)].name,
                                           static_cast<std::size_t>(offset.index));
  if (morph.type == 8U) {
    if (offset.index < 0)
      return i18n::tr("材質 全材質");
    if (static_cast<std::size_t>(offset.index) < session.document.model().materials.size())
      return i18n::tr("材質 ") + indexedOffsetName(session.document.model().materials[
                                                 static_cast<std::size_t>(offset.index)].name,
                                             static_cast<std::size_t>(offset.index));
  }
  if (morph.type == 10U && offset.index >= 0 &&
      static_cast<std::size_t>(offset.index) < session.document.model().rigidBodies.size())
    return i18n::tr("剛体 ") + indexedOffsetName(session.document.model().rigidBodies[
                                               static_cast<std::size_t>(offset.index)].name,
                                           static_cast<std::size_t>(offset.index));
  return (morph.type == 2U ? i18n::tr("ボーン ") : i18n::tr("頂点 ")) +
         std::to_string(offset.index);
}

[[maybe_unused]] bool containsOffsetQuery(std::string_view label, std::string_view query,
                         std::size_t index) {
  return query.empty() || label.find(query) != std::string_view::npos ||
         std::to_string(index).find(query) != std::string::npos;
}

void beginMorphOffsetTargetPick(DocumentSession &session,
                                mmd::MorphHandle morph,
                                std::size_t offsetIndex,
                                std::uint8_t type, bool adding) {
  session.ui.morphOffsetTarget = {
      true, adding, offsetIndex, morph, morphOffsetTargetKind(type), std::nullopt};
  switch (session.ui.morphOffsetTarget.expectedKind) {
  case SelectionKind::vertex:
    session.ui.selectionMode = ViewportSelectionMode::vertex;
    break;
  case SelectionKind::bone:
    session.ui.selectionMode = ViewportSelectionMode::bone;
    break;
  case SelectionKind::material:
    session.ui.selectionMode = ViewportSelectionMode::material;
    break;
  case SelectionKind::rigidBody:
    session.ui.selectionMode = ViewportSelectionMode::rigidBody;
    break;
  default:
    break;
  }
  setStatus(session, i18n::tr("ビューポートからモーフオフセットの対象を選択してください"),
            UiStatusKind::info);
}

bool addMorphOffset(DocumentSession &session, mmd::MorphHandle handle,
                    std::uint8_t type) {
  const auto target = session.ui.morphAddTarget.has_value()
                          ? session.ui.morphAddTarget
                          : session.ui.morphOffsetTarget.target;
  switch (type) {
  case 0:
  case 9:
    if (!target || target->kind != SelectionKind::morph ||
        selectionHandle<mmd::MorphTag>(session.document, *target) == handle)
      return false;
    if (type == 0)
      return addGroupMorphOffset(
          session, handle,
          selectionHandle<mmd::MorphTag>(session.document, *target), 1.0F);
    return addFlipMorphOffset(
        session, handle,
        selectionHandle<mmd::MorphTag>(session.document, *target), 1.0F);
  case 1:
    if (target && target->kind == SelectionKind::vertex)
      return addVertexMorphOffset(session, handle,
                                  selectionHandle<mmd::VertexTag>(session.document, *target),
                                  {});
    return false;
  case 2:
    if (target && target->kind == SelectionKind::bone)
      return addBoneMorphOffset(session, handle,
                                selectionHandle<mmd::BoneTag>(session.document, *target),
                                {}, {0.0F, 0.0F, 0.0F, 1.0F});
    return false;
  case 3:
  case 4:
  case 5:
  case 6:
  case 7: {
    const auto channel = static_cast<std::uint32_t>(type - 3U);
    if (!target || target->kind != SelectionKind::vertex)
      return false;
    return addUvMorphOffset(
        session, handle,
        selectionHandle<mmd::VertexTag>(session.document, *target), channel, {});
  }
  case 8:
    return addMaterialMorphOffset(session, handle,
                                  target && target->kind == SelectionKind::material
                                      ? std::optional{selectionHandle<mmd::MaterialTag>(session.document, *target)}
                                      : std::nullopt,
                                  0, {});
  case 10: {
    if (!target || target->kind != SelectionKind::rigidBody)
      return false;
    return addImpulseMorphOffset(
        session, handle,
        selectionHandle<mmd::RigidBodyTag>(session.document, *target), {}, {}, false);
  }
  default:
    return false;
  }
}

void morphInspector(DocumentSession &session, WorkspaceUiState &workspace,
                    const SelectionItem &selected) {
  const auto handle = selectionHandle<mmd::MorphTag>(session.document, selected);
  const auto *value = session.document.resolve(handle);
  if (value == nullptr)
    return;
  if (!session.ui.morphDraft)
    session.ui.morphDraft = *value;
  auto &draft = *session.ui.morphDraft;
  bool commit = session.ui.morphOffsetDirty;

  ImGui::SeparatorText(i18n::tr("名前"));
  (void)inputText(i18n::tr("名前"), draft.name);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  (void)inputText(i18n::tr("英語名"), draft.englishName);
  commit |= ImGui::IsItemDeactivatedAfterEdit();
  const char *panels[]{i18n::tr("予約"), i18n::tr("まゆ"), i18n::tr("目"), i18n::tr("リップ"), i18n::tr("その他")};
  commit |= enumCombo(i18n::tr("表示区分"), draft.panel, panels, std::size(panels));
  const char *types[]{i18n::tr("グループ"), i18n::tr("頂点"), i18n::tr("ボーン"), "UV", i18n::tr("追加UV1"), i18n::tr("追加UV2"),
                                       i18n::tr("追加UV3"), i18n::tr("追加UV4"), i18n::tr("材質"), i18n::tr("フリップ"), i18n::tr("インパルス")};
  if (draft.offsets.empty()) {
    const auto typeChanged = enumCombo(i18n::tr("種類"), draft.type, types, std::size(types));
    commit |= typeChanged;
    if (typeChanged) {
      session.ui.morphAddTarget.reset();
      session.ui.morphOffsetTarget = {};
    }
  } else {
    const auto type = std::min<std::size_t>(draft.type, std::size(types) - 1U);
    ImGui::Text(i18n::tr("種類: %s"), types[type]);
    ImGui::TextDisabled("%s", i18n::tr("オフセットがあるモーフの種類は変更できません"));
  }
  ImGui::Text(i18n::tr("オフセット: %zu"), draft.offsets.size());

  ImGui::SeparatorText(i18n::tr("オフセット編集"));
  if (!draft.offsets.empty()) {
    (void)drawMorphOffsetBrowser(session, draft, session.ui.morphOffsetIndex,
                                 "inspector", handle);
    auto &offset = draft.offsets[session.ui.morphOffsetIndex];
    bool offsetCommit{};
    if (draft.type != 0 && draft.type != 9)
      offsetCommit = ImGui::InputInt(i18n::tr("対象インデックス"), &offset.index);
    switch (draft.type) {
    case 0:
    case 9:
      offsetCommit |= ImGui::InputFloat(i18n::tr("重み"), &offset.scalar);
      break;
    case 1:
      offsetCommit |= ImGui::InputFloat3(i18n::tr("移動"), offset.vector3.data());
      break;
    case 2:
      offsetCommit |= ImGui::InputFloat3(i18n::tr("移動"), offset.vector3.data());
      offsetCommit |= ImGui::InputFloat4(i18n::tr("回転"), offset.vector4.data());
      break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
      offsetCommit |= ImGui::InputFloat4(i18n::tr("UV移動"), offset.vector4.data());
      break;
    case 8:
      for (std::size_t index = 0; index < 7U; ++index) {
        ImGui::PushID(static_cast<int>(index));
        offsetCommit |= ImGui::InputFloat4(i18n::tr("材質値"), offset.materialVectors[index].data());
        ImGui::PopID();
      }
      break;
    case 10:
      offsetCommit |= ImGui::InputFloat3(i18n::tr("速度"), offset.vector3.data());
      offsetCommit |= ImGui::InputFloat3(i18n::tr("トルク"), offset.tertiaryVector3.data());
      offsetCommit |= ImGui::Checkbox(i18n::tr("ローカル"), &offset.local);
      break;
    default:
      break;
    }
    if (offsetCommit)
      commit = true;
    const auto targetKind = morphOffsetTargetKind(draft.type);
    ImGui::Text(i18n::tr("対象: %s"), targetKind == SelectionKind::vertex ? i18n::tr("頂点") :
                             targetKind == SelectionKind::bone ? i18n::tr("ボーン") :
                             targetKind == SelectionKind::material ? i18n::tr("材質") :
                             targetKind == SelectionKind::rigidBody ? i18n::tr("剛体") : i18n::tr("モーフ"));
    if (draft.type == 0 || draft.type == 9) {
      offsetCommit |= morphIndexCombo(i18n::tr("対象モーフ"), session, offset.index, handle);
      if (offsetCommit)
        commit = true;
    } else if (ImGui::Button(i18n::tr("ビューポートから対象を選択"))) {
      beginMorphOffsetTargetPick(session, handle, session.ui.morphOffsetIndex,
                                 draft.type, false);
    }
    if (offsetCommit)
      ++session.ui.morphOffsetFilterEpoch;
    if (session.ui.morphOffsetTarget.picking)
      ImGui::TextDisabled("%s", i18n::tr("対象を選択中…"));
    if (session.ui.morphOffsetTarget.target)
      ImGui::TextDisabled(i18n::tr("対象ID: %llu"), static_cast<unsigned long long>(session.ui.morphOffsetTarget.target->id));
  }
  ImGui::SeparatorText(i18n::tr("新しいオフセット"));
  if (draft.type == 0 || draft.type == 9) {
    auto addIndex = std::int32_t{-1};
    if (session.ui.morphAddTarget) {
      for (std::size_t index = 0; index < session.document.model().morphs.size(); ++index)
        if (session.document.morphHandle(index) ==
            selectionHandle<mmd::MorphTag>(session.document, *session.ui.morphAddTarget))
          addIndex = static_cast<std::int32_t>(index);
    }
    if (morphIndexCombo(i18n::tr("追加対象モーフ"), session, addIndex, handle)) {
      if (addIndex >= 0)
        session.ui.morphAddTarget = SelectionItem{
            SelectionKind::morph,
            session.document.morphHandle(static_cast<std::size_t>(addIndex)).domain,
            session.document.morphHandle(static_cast<std::size_t>(addIndex)).id,
            session.document.morphHandle(static_cast<std::size_t>(addIndex)).generation};
      else
        session.ui.morphAddTarget.reset();
    }
  } else if (draft.type != 8) {
    if (ImGui::Button(i18n::tr("ビューポートから対象を選択")))
      beginMorphOffsetTargetPick(session, handle, draft.offsets.size(),
                                 draft.type, true);
  } else {
    ImGui::TextDisabled("%s", i18n::tr("対象材質: 全材質 (-1)"));
  }
  const auto addLabel = std::string{"+ "} +
                        types[std::min<std::size_t>(draft.type,
                                                    std::size(types) - 1U)] +
                        i18n::tr("オフセット");
  const auto addTargetRequired = draft.type != 8;
  const auto hasAddTarget = session.ui.morphAddTarget.has_value();
  const auto addEnabled = !addTargetRequired || hasAddTarget;
  if (!addEnabled)
    ImGui::BeginDisabled();
  if (ImGui::Button(addLabel.c_str())) {
    if (addMorphOffset(session, handle, draft.type)) {
      session.ui.morphDraft.reset();
      session.ui.morphOffsetTarget = {};
      session.ui.morphAddTarget.reset();
      setStatus(session, i18n::tr("モーフオフセットを追加しました"),
                UiStatusKind::success);
    } else {
      setStatus(session, i18n::tr("モーフオフセットを追加できません"),
                UiStatusKind::error, std::chrono::milliseconds::zero(), true);
    }
  }
  if (!addEnabled)
    ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(i18n::tr("詳細編集")))
    workspace.showMorph = true;

  ImGui::SeparatorText(i18n::tr("プレビュー"));
  auto &values = session.preview.morphValues;
  auto preview = std::find_if(values.begin(), values.end(),
                              [&](const auto &item) { return item.morph == handle; });
  float weight = preview == values.end() ? 0.0F : preview->weight;
  if (ImGui::SliderFloat(i18n::tr("プレビュー強度"), &weight, 0.0F, 1.0F, "%.2f")) {
    morph::setBlend(session, handle, weight);
  }
  if (ImGui::Button(i18n::tr("このモーフをリセット"))) {
    morph::clearBlend(session, handle);
  }
  ImGui::SameLine();
  if (ImGui::Button(i18n::tr("すべてリセット"))) {
    morph::resetMix(session);
  }
  if (!values.empty())
    ImGui::TextDisabled(i18n::tr("%zu個のモーフを同時プレビュー中"), values.size());

  if (commit) {
    const auto result = editMorph(session, handle, draft);
    setOperationStatus(session, result.success, i18n::tr("モーフを更新しました"),
                       result.message);
    if (result.success) {
      session.ui.morphDraft.reset();
      session.ui.morphOffsetTarget = {};
      session.ui.morphOffsetDirty = false;
    }
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
          texturePreviewCard(session, i18n::tr("プレビュー"),
                             static_cast<std::int32_t>(index), renderer);
          break;
        }
      }
      if (!session.ui.textureDraft)
        session.ui.textureDraft = *texture;
      auto &draft = *session.ui.textureDraft;
      ImGui::SeparatorText(i18n::tr("パス"));
      (void)inputText(i18n::tr("保存パス"), draft.storedPath);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const auto result = editTexture(session, handle, draft);
        setOperationStatus(session, result.success, i18n::tr("テクスチャを更新しました"),
                           result.message);
        session.ui.textureDraft.reset();
        return;
      }
      const auto path = textureIndex
                            ? mmd::pmx::resolveTexturePath(model, *textureIndex)
                            : std::filesystem::path{};
      ImGui::TextWrapped("%s", path.string().c_str());
      ImGui::TextUnformatted(!path.empty() && std::filesystem::exists(path)
                                 ? i18n::tr("✓ 読み込み可能")
                                 : i18n::tr("⚠ ファイルがありません"));
    }
  }
}

} // namespace

void drawInspectorPanel(DocumentSession &session, GpuModelRenderer *renderer,
                        WorkspaceUiState &workspace, bool *open) {
  auto &activeWorkspace = workspace.active;
  if (!ImGui::Begin(i18n::windowTitle("インスペクター"), open)) {
    ImGui::End();
    return;
  }
  if (session.selection.items().empty()) {
    switch (activeWorkspace) {
    case EditorWorkspace::model:
        ImGui::TextUnformatted(i18n::tr("モデル"));
        ImGui::TextWrapped("%s", i18n::tr("アウトライナーから材質またはテクスチャを選択してください"));
        break;
    case EditorWorkspace::rig:
        ImGui::TextUnformatted(i18n::tr("リグ"));
        ImGui::TextWrapped("%s", i18n::tr("頂点またはボーンを選択してください"));
        break;
    case EditorWorkspace::morph:
        ImGui::TextUnformatted(i18n::tr("モーフ"));
        ImGui::TextWrapped("%s", i18n::tr("モーフをアウトライナーから選択してください"));
        break;
    case EditorWorkspace::physics:
        ImGui::TextUnformatted(i18n::tr("物理"));
        ImGui::TextWrapped("%s", i18n::tr("剛体、ジョイント、ソフトボディを選択してください"));
        break;
    case EditorWorkspace::inspect:
        ImGui::TextWrapped("%s", i18n::tr("Outlinerまたはビューポートで対象を選択してください"));
        break;
    }
    ImGui::End();
    return;
  }
  const auto selected = session.selection.items().front();
  if (!workspacePolicy(activeWorkspace).allows(selected.kind)) {
    ImGui::TextDisabled("%s", i18n::tr("このワークスペースでは選択対象を編集できません"));
    ImGui::End();
    return;
  }
  ImGui::Text("%s", kindName(selected.kind));
  if (session.selection.items().size() > 1U) {
    ImGui::SameLine();
    ImGui::TextDisabled(i18n::tr("(%zu件選択)"), session.selection.items().size());
  }
  switch (selected.kind) {
  case SelectionKind::material:
    materialInspector(session, selected, renderer);
    break;
  case SelectionKind::bone:
    boneInspector(session, selected);
    break;
  case SelectionKind::vertex:
    vertexInspector(session, activeWorkspace, selected);
    break;
  case SelectionKind::rigidBody:
    rigidBodyInspector(session, selected);
    break;
  case SelectionKind::joint:
    jointInspector(session, selected);
    break;
  case SelectionKind::morph:
    morphInspector(session, workspace, selected);
    break;
  default:
    readOnlyInspector(session, selected, renderer);
    break;
  }
  ImGui::End();
}

void drawStatusBar(DocumentSession &session, bool &showDiagnostics,
                   bool &showReferences, bool &showDiff) {
  updateStatusLifetime(session);
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
    ImGui::TextUnformatted(session.hasUnsavedWork() ? i18n::tr("● 未保存") : i18n::tr("✓ 保存済み"));
    ImGui::SameLine();
    if (ImGui::SmallButton(
            (i18n::tr("診断 ") + std::to_string(session.validation.issues.size()))
                .c_str()))
      showDiagnostics = true;
    ImGui::SameLine();
    const auto selected = selectionSummary(session);
    if (ImGui::SmallButton(selected.c_str()))
      showReferences = true;
    ImGui::SameLine();
    if (ImGui::SmallButton(i18n::tr("差分")))
      showDiff = true;
    if (!session.ui.status.empty()) {
      ImGui::SameLine();
      const auto color = session.ui.statusKind == UiStatusKind::success
                             ? ImVec4{0.35F, 0.85F, 0.45F, 1.0F}
                         : session.ui.statusKind == UiStatusKind::warning
                             ? ImVec4{1.0F, 0.75F, 0.3F, 1.0F}
                         : session.ui.statusKind == UiStatusKind::error
                             ? ImVec4{1.0F, 0.35F, 0.35F, 1.0F}
                             : ImVec4{0.7F, 0.75F, 0.82F, 1.0F};
      ImGui::TextColored(color, "%s", session.ui.status.c_str());
    }
  }
  ImGui::End();
}

} // namespace pmxer
