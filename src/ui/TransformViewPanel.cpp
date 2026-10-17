#include "i18n/Language.hpp"

#include "TransformViewPanel.hpp"
#include "FlowLayout.hpp"
#include "EditorPanels.hpp"

#include "../editor/DeformController.hpp"
#include "../editor/DocumentSession.hpp"
#include "../editor/UiStatus.hpp"
#include "../editor/morph/MorphCapture.hpp"
#include "../editor/morph/MorphMask.hpp"
#include "../editor/morph/MorphMixer.hpp"
#include "../editor/morph/MorphOps.hpp"
#include "../editor/ViewportCapabilities.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmxer {
namespace {

int resizeTextCallback(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;
    auto *value = static_cast<std::string *>(data->UserData);
    value->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = value->data();
    return 0;
}

bool inputString(const char *label, std::string &value) {
    if (value.capacity() < value.size() + 1U)
        value.reserve(value.size() + 1U);
    value.resize(value.size());
    const auto changed = ImGui::InputText(label, value.data(), value.capacity() + 1U,
                                          ImGuiInputTextFlags_CallbackResize,
                                          resizeTextCallback, &value);
    value.resize(std::strlen(value.c_str()));
    return changed;
}

std::optional<std::size_t> morphIndex(const DocumentSession &session,
                                      mmd::MorphHandle handle) {
    const auto &model = session.document.model();
    for (std::size_t index = 0; index < model.morphs.size(); ++index)
        if (session.document.morphHandle(index) == handle)
            return index;
    return std::nullopt;
}

std::size_t currentMorphIndex(const DocumentSession &session) {
    for (const auto &item : session.selection.items()) {
        if (item.kind != SelectionKind::morph)
            continue;
        const auto index = morphIndex(session, selectionHandle<mmd::MorphTag>(session.document, item));
        if (index)
            return *index;
    }
    if (session.document.model().morphs.empty())
        return 0U;
    return std::min(session.ui.morphIndex, session.document.model().morphs.size() - 1U);
}

bool morphCombo(const char *label, const mmd::PmxModel &model, std::size_t &index) {
    if (model.morphs.empty()) {
        ImGui::TextDisabled("%s", i18n::tr("モーフがありません"));
        return false;
    }
    index = std::min(index, model.morphs.size() - 1U);
    bool changed = false;
    if (ImGui::BeginCombo(label, model.morphs[index].name.c_str())) {
        for (std::size_t candidate = 0; candidate < model.morphs.size(); ++candidate) {
            const bool selected = candidate == index;
            ImGui::PushID(static_cast<int>(candidate));
            if (ImGui::Selectable(model.morphs[candidate].name.c_str(), selected)) {
                index = candidate;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool optionalMorphCombo(const char *label, const mmd::PmxModel &model,
                        std::size_t &selectedIndex) {
    const auto preview = selectedIndex < model.morphs.size()
                             ? model.morphs[selectedIndex].name.c_str()
                             : i18n::tr("（モーフを選択…）");
    bool changed = false;
    if (!ImGui::BeginCombo(label, preview))
        return false;
    if (ImGui::Selectable(i18n::tr("（モーフを選択…）"), selectedIndex == model.morphs.size())) {
        selectedIndex = model.morphs.size();
        changed = true;
    }
    for (std::size_t candidate = 0; candidate < model.morphs.size(); ++candidate) {
        const bool selected = selectedIndex == candidate;
        ImGui::PushID(static_cast<int>(candidate));
        if (ImGui::Selectable(model.morphs[candidate].name.c_str(), selected)) {
            selectedIndex = candidate;
            changed = true;
        }
        if (selected)
            ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return changed;
}

void status(DocumentSession &session, const OperationResult &result,
            std::string success) {
    setOperationStatus(session, result.success, std::move(success), result.message);
}

const char *morphTypeName(std::uint8_t type) {
    switch (type) {
    case 0U:
        return i18n::tr("グループモーフ");
    case 1U:
        return i18n::tr("頂点モーフ");
    case 2U:
        return i18n::tr("ボーンモーフ");
    case 8U:
        return i18n::tr("材質モーフ");
    case 9U:
        return i18n::tr("フリップモーフ");
    case 10U:
        return i18n::tr("インパルスモーフ");
    default:
        return i18n::tr("UVモーフ");
    }
}

void drawCaptureName(std::string &name) {
    ui::fitInputWidth(i18n::tr("名前"));
    inputString(i18n::tr("名前"), name);
}

std::size_t selectedCount(const DocumentSession &session, SelectionKind kind) {
    return static_cast<std::size_t>(std::count_if(
        session.selection.items().begin(), session.selection.items().end(),
        [kind](const auto &item) { return item.kind == kind; }));
}

void drawDisabledReason(std::string_view reason) {
    if (!reason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(reason.data(), reason.data() + reason.size());
        ImGui::EndTooltip();
    }
}

void drawTransformToolButton(DocumentSession &session, const char *label,
                             ViewportTool tool, bool enabled,
                             std::string_view reason = {}) {
    ImGui::BeginDisabled(!enabled);
    if (ImGui::RadioButton(label, session.ui.viewportTool == tool))
        session.ui.viewportTool = tool;
    ImGui::EndDisabled();
    if (!enabled)
        drawDisabledReason(reason);
}

void drawVertexTransform(DocumentSession &session, WorkspaceUiState &workspace) {
    session.deform.mode = DeformMode::shape;
    ImGui::SeparatorText(i18n::tr("頂点変形"));
    const auto selectedVertices = selectedCount(session, SelectionKind::vertex);
    const auto modifiedVertices = session.deform.vertices.size();
    ImGui::Text(i18n::tr("選択中: %zu 頂点"), selectedVertices);
    ImGui::Text(i18n::tr("一時変形中: %zu 頂点"), modifiedVertices);
    ImGui::PushTextWrapPos(0.0F);
    if (selectedVertices == 0U) {
        ImGui::TextColored(ImVec4{0.35F, 0.7F, 1.0F, 1.0F},
                           "%s", i18n::tr("● 1. ビューポートで変形する頂点を選択してください"));
        ImGui::TextDisabled("%s", i18n::tr("  クリックまたはドラッグで選択できます"));
        ImGui::TextDisabled("%s", i18n::tr("○ 2. 変形"));
        ImGui::TextDisabled("%s", i18n::tr("○ 3. モーフを作成"));
    } else if (modifiedVertices == 0U) {
        ImGui::TextUnformatted(i18n::tr("✓ 1. 頂点を選択"));
        ImGui::TextColored(ImVec4{0.35F, 0.7F, 1.0F, 1.0F},
                           "%s", i18n::tr("● 2. 移動・回転・拡縮で頂点を変形してください"));
        ImGui::TextDisabled("%s", i18n::tr("○ 3. モーフを作成"));
    } else {
        ImGui::TextUnformatted(i18n::tr("✓ 1. 頂点を選択"));
        ImGui::Text(i18n::tr("✓ 2. %zu 頂点を変形"), modifiedVertices);
        ImGui::TextColored(ImVec4{0.35F, 0.7F, 1.0F, 1.0F},
                           "%s", i18n::tr("● 3. 頂点モーフとして作成できます"));
    }

    ImGui::PopTextWrapPos();
    ImGui::SeparatorText(i18n::tr("操作"));
    drawTransformToolButton(session, i18n::tr("選択"), ViewportTool::select, true);
    ui::sameLineIfFits(i18n::tr("移動 G"));
    drawTransformToolButton(session, i18n::tr("移動 G"), ViewportTool::move,
                            selectedVertices != 0U, i18n::tr("先に頂点を選択してください"));
    ui::sameLineIfFits(i18n::tr("回転 R"));
    drawTransformToolButton(session, i18n::tr("回転 R"), ViewportTool::rotate,
                            selectedVertices != 0U, i18n::tr("先に頂点を選択してください"));
    ui::sameLineIfFits(i18n::tr("拡縮 S"));
    drawTransformToolButton(session, i18n::tr("拡縮 S"), ViewportTool::scale,
                            selectedVertices != 0U, i18n::tr("先に頂点を選択してください"));
    ui::fitInputWidth(i18n::tr("ピボット"));
    if (ImGui::BeginCombo(i18n::tr("ピボット"), session.deform.pivotMode == PivotMode::median ? i18n::tr("中央値") :
                                               session.deform.pivotMode == PivotMode::active ? i18n::tr("アクティブ") : i18n::tr("原点"))) {
        if (ImGui::Selectable(i18n::tr("中央値"), session.deform.pivotMode == PivotMode::median))
            session.deform.pivotMode = PivotMode::median;
        if (ImGui::Selectable(i18n::tr("アクティブ"), session.deform.pivotMode == PivotMode::active))
            session.deform.pivotMode = PivotMode::active;
        if (ImGui::Selectable(i18n::tr("原点"), session.deform.pivotMode == PivotMode::origin))
            session.deform.pivotMode = PivotMode::origin;
        ImGui::EndCombo();
    }
    ImGui::Checkbox(i18n::tr("X軸対称"), &session.deform.symmetryX);
    if (session.deform.symmetryX) {
        ImGui::DragFloat(i18n::tr("中心 X"), &session.deform.symmetryCenterX, 0.001F);
        ImGui::DragFloat(i18n::tr("許容誤差"), &session.deform.symmetryTolerance, 0.001F, 0.0F, 10.0F);
        ImGui::Checkbox(i18n::tr("対称側を入れ替える"), &session.deform.symmetrySwapSides);
    }
    ImGui::SeparatorText(i18n::tr("モーフとして保存"));
    drawCaptureName(session.deform.vertexCaptureName);
    const bool canCapture = modifiedVertices != 0U;
    ImGui::BeginDisabled(!canCapture);
    if (ImGui::Button(i18n::tr("頂点モーフを作成"))) {
        const auto result = morph::captureVertexMorph(session, session.deform.vertexCaptureName);
        status(session, result, i18n::tr("頂点モーフを作成しました"));
        if (result.success) {
            workspace.active = EditorWorkspace::morph;
            applyWorkspacePolicy(session, workspacePolicy(workspace.active));
            applyViewportProfile(session, workspace.viewportProfiles[workspaceIndex(workspace.active)]);
            session.deform.tab = TransformViewTab::morph;
            session.deform.mode = DeformMode::inactive;
        }
    }
    ImGui::EndDisabled();
    if (!canCapture)
        drawDisabledReason(i18n::tr("まだ頂点が変形されていません"));
    if (modifiedVertices != 0U && ImGui::Button(i18n::tr("この頂点編集を破棄")))
        discardPendingVertexEdit(session);
    ImGui::TextDisabled("%s", i18n::tr("ⓘ モーフを作成するまでPMX本体には反映されません"));
}

void drawBoneTransform(DocumentSession &session, WorkspaceUiState &workspace) {
    session.deform.mode = DeformMode::pose;
    ImGui::SeparatorText(i18n::tr("ボーン変形"));
    const auto selectedBones = selectedCount(session, SelectionKind::bone);
    const auto modifiedBones = session.deform.bones.size();
    ImGui::Text(i18n::tr("選択中: %zu ボーン"), selectedBones);
    ImGui::Text(i18n::tr("一時変形中: %zu ボーン"), modifiedBones);
    ActionAvailability selectedAvailability{
        selectedBones == 1U, selectedBones == 1U ? SupportLevel::supported
                                                 : SupportLevel::unsupported,
        selectedBones == 0U ? i18n::tr("ビューポートでボーンを1本選択してください")
                            : i18n::tr("一度に操作できるボーンは1本です")};
    if (selectedBones == 1U) {
        const auto item = *std::find_if(
            session.selection.items().begin(), session.selection.items().end(),
            [](const auto &value) { return value.kind == SelectionKind::bone; });
        const auto handle = selectionHandle<mmd::BoneTag>(session.document, item);
        selectedAvailability = boneTransformAvailability(session, handle);
    }
    if (!selectedAvailability.enabled)
        ImGui::TextWrapped("ⓘ %s", selectedAvailability.reason.data());
    ImGui::TextDisabled("%s", i18n::tr("複数のボーンを順番に変形して1つのボーンモーフにできます"));
    ImGui::SeparatorText(i18n::tr("操作"));
    drawTransformToolButton(session, i18n::tr("選択"), ViewportTool::select, true);
    ui::sameLineIfFits(i18n::tr("移動 G"));
    drawTransformToolButton(session, i18n::tr("移動 G"), ViewportTool::move,
                            selectedAvailability.enabled, selectedAvailability.reason);
    ui::sameLineIfFits(i18n::tr("回転 R"));
    drawTransformToolButton(session, i18n::tr("回転 R"), ViewportTool::rotate,
                            selectedAvailability.enabled, selectedAvailability.reason);
    ImGui::Checkbox(i18n::tr("物理プレビュー"), &session.previewPhysics);
    ImGui::Checkbox(i18n::tr("IKプレビュー"), &session.previewIk);
    ImGui::SeparatorText(i18n::tr("モーフとして保存"));
    drawCaptureName(session.deform.boneCaptureName);
    const auto invalidPending = std::find_if(
        session.deform.bones.begin(), session.deform.bones.end(),
        [&](const auto &delta) { return !boneTransformAvailability(session, delta.bone).enabled; });
    const bool canCapture = modifiedBones != 0U && invalidPending == session.deform.bones.end();
    const std::string_view captureReason = modifiedBones == 0U
                                               ? i18n::tr("まだボーンが変形されていません")
                                               : invalidPending != session.deform.bones.end()
                                                     ? boneTransformAvailability(session, invalidPending->bone).reason
                                                     : std::string_view{};
    ImGui::BeginDisabled(!canCapture);
    if (ImGui::Button(i18n::tr("ボーンモーフを作成"))) {
        const auto result = morph::captureBoneMorph(session, session.deform.boneCaptureName);
        status(session, result, i18n::tr("ボーンモーフを作成しました"));
        if (result.success) {
            workspace.active = EditorWorkspace::morph;
            applyWorkspacePolicy(session, workspacePolicy(workspace.active));
            applyViewportProfile(session, workspace.viewportProfiles[workspaceIndex(workspace.active)]);
            session.deform.tab = TransformViewTab::morph;
            session.deform.mode = DeformMode::inactive;
        }
    }
    ImGui::EndDisabled();
    if (!canCapture)
        drawDisabledReason(captureReason);
    if (modifiedBones != 0U && ImGui::Button(i18n::tr("このボーン編集を破棄")))
        discardPendingBoneEdit(session);
    ImGui::TextDisabled("%s", i18n::tr("ⓘ モーフを作成するまでPMX本体には反映されません"));
}

float blendWeight(const PreviewSession &preview, mmd::MorphHandle handle) {
    const auto found = std::find_if(preview.morphValues.begin(), preview.morphValues.end(),
                                    [&](const auto &blend) { return blend.morph == handle; });
    return found == preview.morphValues.end() ? 0.0F : found->weight;
}

void drawMorphMixer(DocumentSession &session) {
    session.deform.mode = DeformMode::inactive;
    ImGui::SeparatorText(i18n::tr("モーフミキサー"));
    auto &model = session.document.model();
    ImGui::InputText(i18n::tr("検索"), session.deform.morphSearch.data(), session.deform.morphSearch.size());
    const std::string_view search(session.deform.morphSearch.data());
    for (std::size_t index = 0; index < model.morphs.size(); ++index) {
        const auto &morphValue = model.morphs[index];
        if (!search.empty() && morphValue.name.find(search) == std::string::npos)
            continue;
        const auto handle = session.document.morphHandle(index);
        auto weight = blendWeight(session.preview, handle);
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextUnformatted(morphValue.name.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-90.0F);
        if (ImGui::SliderFloat("##weight", &weight, 0.0F, 1.0F, "%.2f"))
            morph::setBlend(session, handle, weight);
        ImGui::SameLine();
        const bool solo = session.preview.solo && session.preview.soloMorph == handle;
        if (ImGui::SmallButton(solo ? i18n::tr("ソロ解除") : i18n::tr("ソロ"))) {
            if (solo)
                morph::clearSoloMorph(session);
            else
                morph::setSoloMorph(session, handle);
        }
        ImGui::PopID();
    }
    if (ImGui::Button(i18n::tr("すべてリセット")))
        morph::resetMix(session);
    ImGui::SeparatorText(i18n::tr("現在のミックスから作成"));
    drawCaptureName(session.deform.groupCaptureName);
    const auto effectiveMix = morph::effectiveMorphMix(session);
    const auto hasMix = std::any_of(
        effectiveMix.begin(), effectiveMix.end(),
        [](const auto &blend) { return std::abs(blend.weight) > 1e-7F; });
    ImGui::BeginDisabled(!hasMix);
    if (ImGui::Button(i18n::tr("グループモーフを作成")))
        status(session, morph::captureGroupMorph(session, session.deform.groupCaptureName),
               i18n::tr("グループモーフを作成しました"));
    if (!hasMix)
        drawDisabledReason(i18n::tr("ミックスするモーフの値を変更してください"));
    if (ImGui::Button(i18n::tr("頂点モーフへベイク"))) {
        const auto analysis = morph::analyzeVertexMix(session);
        if (analysis.ignoredTypes.empty()) {
            status(session, morph::bakeMixAsVertexMorph(session, session.deform.groupCaptureName),
                   i18n::tr("頂点モーフへベイクしました"));
        } else {
            session.deform.confirmVertexBake = true;
            session.deform.vertexBakeIgnoredTypes.assign(analysis.ignoredTypes.begin(),
                                                         analysis.ignoredTypes.end());
            ImGui::OpenPopup(i18n::tr("頂点成分のベイクを確認"));
        }
    }
    ImGui::EndDisabled();
    if (session.deform.confirmVertexBake &&
        ImGui::BeginPopupModal(i18n::tr("頂点成分のベイクを確認"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", i18n::tr("このミックスには次の種類も含まれます:"));
        for (const auto type : session.deform.vertexBakeIgnoredTypes)
            ImGui::BulletText("%s", morphTypeName(type));
        ImGui::TextWrapped("%s", i18n::tr("これらは頂点モーフには含まれません。"));
        if (ImGui::Button(i18n::tr("頂点成分をベイク"))) {
            const auto result = morph::bakeMixAsVertexMorph(
                session, session.deform.groupCaptureName, {.allowIgnoredTypes = true});
            status(session, result, i18n::tr("頂点モーフへベイクしました"));
            session.deform.confirmVertexBake = false;
            session.deform.vertexBakeIgnoredTypes.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr("キャンセル"))) {
            session.deform.confirmVertexBake = false;
            session.deform.vertexBakeIgnoredTypes.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void drawMorphOperations(DocumentSession &session) {
    auto &model = session.document.model();
    if (model.morphs.empty())
        return;
    if (!ImGui::CollapsingHeader(i18n::tr("高度なモーフ操作")))
        return;
    auto index = currentMorphIndex(session);
    if (morphCombo(i18n::tr("対象モーフ"), model, index))
        session.selection.clear();
    session.ui.morphIndex = index;
    const auto source = model.morphs[index];
    const auto sourceHandle = session.document.morphHandle(index);
    ImGui::SeparatorText(i18n::tr("高度なモーフ操作"));
    ImGui::Text(i18n::tr("対象: %s"), source.name.c_str());
    ImGui::DragFloat(i18n::tr("倍率"), &session.deform.morphScaleFactor, 0.05F, -10.0F, 10.0F);
    if (ImGui::Button(i18n::tr("差分を拡縮")))
        status(session, morph::createMorphFromData(session,
                                                   morph::scale(morph::copy(source), session.deform.morphScaleFactor),
                                                   source.name + i18n::tr(" 拡縮"), i18n::tr("モーフを拡縮")),
               i18n::tr("拡縮したモーフを作成しました"));
    ImGui::SameLine();
    if (ImGui::Button(i18n::tr("差分を反転"))) {
        const auto inverted = morph::invert(morph::copy(source));
        if (!inverted.success)
            setOperationStatus(session, false, i18n::tr("反転モーフを作成できませんでした"), inverted.message);
        else
            status(session, morph::createMorphFromData(
                              session, inverted.data, source.name + i18n::tr(" 反転"), i18n::tr("モーフ差分を反転")),
                   i18n::tr("反転モーフを作成しました"));
    }
    if (ImGui::Button(i18n::tr("複製")))
        status(session, morph::duplicateMorph(session, sourceHandle, source.name + i18n::tr(" コピー")),
               i18n::tr("モーフを複製しました"));
    ImGui::SameLine();
    if (ImGui::Button(i18n::tr("ベースとモーフを反転"))) {
        const auto references = session.document.referencesTo(sourceHandle);
        std::vector<std::pair<std::uint64_t, std::uint32_t>> owners;
        for (const auto &reference : references) {
            if (reference.ownerKind != mmd::ReferenceObjectKind::morph)
                continue;
            if (std::find(owners.begin(), owners.end(),
                          std::pair{reference.ownerId, reference.ownerGeneration}) == owners.end())
                owners.emplace_back(reference.ownerId, reference.ownerGeneration);
        }
        if (!owners.empty()) {
            session.deform.confirmBakeReverse = true;
            session.deform.bakeReverseMorph = sourceHandle;
            session.deform.bakeReverseReferenceCount = owners.size();
            ImGui::OpenPopup(i18n::tr("ベースとモーフの反転を確認"));
        } else {
            status(session, morph::bakeAndReverseBase(session, sourceHandle),
                   i18n::tr("ベースとモーフを反転しました"));
        }
    }
    if (session.deform.confirmBakeReverse &&
        ImGui::BeginPopupModal(i18n::tr("ベースとモーフの反転を確認"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(i18n::tr("このモーフは %zu 個のグループ／フリップモーフから参照されています。"),
                    session.deform.bakeReverseReferenceCount);
        ImGui::TextWrapped("%s", i18n::tr("反転すると参照元のモーフも変化する可能性があります。"));
        if (ImGui::Button(i18n::tr("反転する"))) {
            const auto result = morph::bakeAndReverseBase(
                session, session.deform.bakeReverseMorph, {.allowReferencedMorph = true});
            status(session, result, i18n::tr("ベースとモーフを反転しました"));
            session.deform.confirmBakeReverse = false;
            session.deform.bakeReverseMorph = {};
            session.deform.bakeReverseReferenceCount = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr("キャンセル"))) {
            session.deform.confirmBakeReverse = false;
            session.deform.bakeReverseMorph = {};
            session.deform.bakeReverseReferenceCount = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // The end index represents no selection in the combo.
    auto otherIndex = morphIndex(session, session.deform.operationMorph).value_or(model.morphs.size());
    if (optionalMorphCombo(i18n::tr("2つ目のモーフ"), model, otherIndex))
        session.deform.operationMorph = otherIndex < model.morphs.size() ? session.document.morphHandle(otherIndex) : mmd::MorphHandle{};
    const auto validOther = otherIndex < model.morphs.size() && otherIndex != index &&
                            model.morphs[otherIndex].type == source.type;
    if (!validOther)
        ImGui::TextDisabled("%s", i18n::tr("同じ種類の別のモーフを選択してください"));
    ImGui::BeginDisabled(!validOther);
    if (ImGui::Button(i18n::tr("モーフを結合"))) {
        const auto &other = model.morphs[otherIndex];
        const std::array<morph::MorphData, 2> values{morph::copy(source), morph::copy(other)};
        status(session, morph::createMorphFromData(session, morph::combine(values),
                                                   source.name + " + " + other.name, i18n::tr("モーフを結合")),
               i18n::tr("結合モーフを作成しました"));
    }
    ImGui::SameLine();
    if (ImGui::Button(i18n::tr("モーフを減算"))) {
        const auto &other = model.morphs[otherIndex];
        const auto subtracted = morph::subtract(morph::copy(source), morph::copy(other));
        if (!subtracted.success)
            setOperationStatus(session, false, i18n::tr("減算モーフを作成できませんでした"), subtracted.message);
        else
            status(session, morph::createMorphFromData(
                              session, subtracted.data, source.name + " - " + other.name, i18n::tr("モーフを減算")),
                   i18n::tr("減算モーフを作成しました"));
    }
    ImGui::EndDisabled();
    if (ImGui::Button(i18n::tr("左右に分割"))) {
        if (source.type != 1U) {
            setStatus(session, i18n::tr("左右分割は頂点モーフだけで使用できます"),
                      UiStatusKind::warning);
        } else {
            const morph::SideSplitOptions options{session.deform.sideSplitCenterX,
                                                   session.deform.sideSplitFeather,
                                                   session.deform.sideSplitSwapSides,
                                                   session.deform.sideSplitDuplicateCenterVertices};
            const auto result = morph::splitSide(model, source, options);
            status(session, morph::createSideSplitMorphs(session, std::move(result.left),
                                                         std::move(result.right), source.name),
                   i18n::tr("左右分割モーフを作成しました"));
        }
    }
    ImGui::DragFloat(i18n::tr("分割中心 X"), &session.deform.sideSplitCenterX, 0.001F);
    ImGui::DragFloat(i18n::tr("分割フェザー"), &session.deform.sideSplitFeather, 0.001F, 0.0F, 10.0F);
    ImGui::Checkbox(i18n::tr("左右を入れ替える"), &session.deform.sideSplitSwapSides);
    ImGui::Checkbox(i18n::tr("中心頂点を複製"), &session.deform.sideSplitDuplicateCenterVertices);
    ImGui::DragInt(i18n::tr("材質インデックス"), &session.deform.materialIndex, 1.0F, -1,
                   static_cast<int>(model.materials.size()) - 1);
    const auto material = session.deform.materialIndex < 0
                              ? std::nullopt
                              : std::optional<std::size_t>(session.deform.materialIndex);
    const auto filterMaterial = [&](morph::MaterialFilterMode mode, const char *suffix) {
        if (source.type != 1U || !material) {
            setStatus(session, i18n::tr("先に材質と頂点モーフを選択してください"),
                      UiStatusKind::warning);
            return;
        }
        const auto filtered = morph::filterByMaterial(model, source, material, mode);
        status(session, morph::createMorphFromData(session, filtered,
                                                   source.name + suffix, i18n::tr("材質マスク")),
               i18n::tr("材質で絞り込んだモーフを作成しました"));
    };
    if (ImGui::Button(i18n::tr("使用材質を除外")))
        filterMaterial(morph::MaterialFilterMode::excludeUsed, i18n::tr(" 材質除外"));
    ImGui::SameLine();
    if (ImGui::Button(i18n::tr("専用材質を除外")))
        filterMaterial(morph::MaterialFilterMode::excludeExclusive, i18n::tr(" 専用材質除外"));
    if (ImGui::Button(i18n::tr("使用材質だけ残す")))
        filterMaterial(morph::MaterialFilterMode::keepOnlyUsed, i18n::tr(" 材質のみ"));
    ImGui::SameLine();
    if (ImGui::Button(i18n::tr("専用材質だけ残す")))
        filterMaterial(morph::MaterialFilterMode::keepOnlyExclusive, i18n::tr(" 専用材質のみ"));
}

} // namespace

void drawTransformView(DocumentSession &session, WorkspaceUiState &workspace,
                       bool *open) {
    if (open != nullptr && !*open) {
        session.deform.engaged = false;
        session.deform.suspended = true;
        session.deform.tabActivated = false;
        session.ui.gizmoDragging = false;
        return;
    }
    if (!ImGui::Begin(i18n::windowTitle("Transform View"), open)) {
        if (open != nullptr && !*open) {
            session.deform.engaged = false;
            session.deform.suspended = true;
            session.deform.tabActivated = false;
            session.ui.gizmoDragging = false;
        } else {
            session.deform.engaged = true;
            session.deform.suspended = false;
        }
        ImGui::End();
        return;
    }
    session.deform.engaged = true;
    session.deform.suspended = false;
    ImGui::PushTextWrapPos(0.0F);
    const auto activateTab = [&](TransformViewTab tab) {
        activateTransformTab(session, workspace.active, tab);
        auto &profile = workspace.viewportProfiles[workspaceIndex(workspace.active)];
        if (tab == TransformViewTab::bone)
            profile.showBones = true;
        applyViewportProfile(session, profile);
    };
    if (!session.deform.tabActivated || session.deform.activatedTab != session.deform.tab)
        activateTab(session.deform.tab);

    const auto vertexTabLabel = session.deform.vertices.empty()
                                    ? std::string(i18n::tr("頂点"))
                                    : i18n::tr("頂点 •") + std::to_string(session.deform.vertices.size());
    const auto boneTabLabel = session.deform.bones.empty()
                                  ? std::string(i18n::tr("ボーン"))
                                  : i18n::tr("ボーン •") + std::to_string(session.deform.bones.size());
    if (ImGui::RadioButton(vertexTabLabel.c_str(), session.deform.tab == TransformViewTab::vertex))
        activateTab(TransformViewTab::vertex);
    ui::sameLineIfFits(boneTabLabel.c_str());
    if (ImGui::RadioButton(boneTabLabel.c_str(), session.deform.tab == TransformViewTab::bone))
        activateTab(TransformViewTab::bone);
    ui::sameLineIfFits(i18n::tr("モーフ"));
    if (ImGui::RadioButton(i18n::tr("モーフ"), session.deform.tab == TransformViewTab::morph))
        activateTab(TransformViewTab::morph);
    ImGui::Separator();
    if (session.deform.tab == TransformViewTab::bone) {
        drawBoneTransform(session, workspace);
    } else if (session.deform.tab == TransformViewTab::morph) {
        drawMorphMixer(session);
        drawMorphOperations(session);
    } else {
        drawVertexTransform(session, workspace);
    }
    if (!session.deform.vertices.empty() && !session.deform.bones.empty()) {
        ImGui::Separator();
        if (ImGui::Button(i18n::tr("すべての一時変形を破棄…")))
            ImGui::OpenPopup(i18n::tr("すべての一時変形を破棄"));
    }
    if (ImGui::BeginPopupModal(i18n::tr("すべての一時変形を破棄"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(i18n::tr("頂点 %zu件、ボーン %zu件の一時変形を破棄します。"),
                    session.deform.vertices.size(), session.deform.bones.size());
        ImGui::TextUnformatted(i18n::tr("この操作は元に戻せません。"));
        if (ImGui::Button(i18n::tr("すべて破棄"))) {
            discardAllPendingTransformEdits(session);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(i18n::tr("キャンセル")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const bool closeRequested = open != nullptr && !*open;
    ImGui::PopTextWrapPos();
    ImGui::End();
    if (closeRequested) {
        session.deform.engaged = false;
        session.deform.suspended = true;
        session.deform.tabActivated = false;
        session.ui.gizmoDragging = false;
    }
}

} // namespace pmxer
