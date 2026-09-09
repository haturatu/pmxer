#include "DeformLabPanel.hpp"

#include "../editor/DeformController.hpp"
#include "../editor/DocumentSession.hpp"
#include "../editor/UiStatus.hpp"
#include "../editor/morph/MorphCapture.hpp"
#include "../editor/morph/MorphMask.hpp"
#include "../editor/morph/MorphMixer.hpp"
#include "../editor/morph/MorphOps.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
        ImGui::TextDisabled("No morphs available");
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

void status(DocumentSession &session, const OperationResult &result,
            std::string success) {
    setOperationStatus(session, result.success, std::move(success), result.message);
}

void drawCaptureName(DocumentSession &session) {
    inputString("Name", session.deform.captureName);
}

void drawVertexTransform(DocumentSession &session) {
    session.deform.mode = DeformMode::shape;
    ImGui::SeparatorText("Vertex Transform");
    const auto selectedVertices = static_cast<std::size_t>(std::count_if(
        session.selection.items().begin(), session.selection.items().end(),
        [](const auto &item) { return item.kind == SelectionKind::vertex; }));
    ImGui::Text("Selected vertices: %zu", selectedVertices);
    ImGui::TextDisabled("Use the viewport Move / Rotate / Scale tools to edit a temporary shape.");
    if (ImGui::BeginCombo("Pivot", session.deform.pivotMode == PivotMode::median ? "Median" :
                                           session.deform.pivotMode == PivotMode::active ? "Active" : "Origin")) {
        if (ImGui::Selectable("Median", session.deform.pivotMode == PivotMode::median))
            session.deform.pivotMode = PivotMode::median;
        if (ImGui::Selectable("Active", session.deform.pivotMode == PivotMode::active))
            session.deform.pivotMode = PivotMode::active;
        if (ImGui::Selectable("Origin", session.deform.pivotMode == PivotMode::origin))
            session.deform.pivotMode = PivotMode::origin;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Mirror X", &session.deform.symmetryX);
    if (session.deform.symmetryX) {
        ImGui::DragFloat("Center X", &session.deform.symmetryCenterX, 0.001F);
        ImGui::DragFloat("Mirror tolerance", &session.deform.symmetryTolerance, 0.001F, 0.0F, 10.0F);
        ImGui::Checkbox("Swap mirror sides", &session.deform.symmetrySwapSides);
    }
    ImGui::Separator();
    drawCaptureName(session);
    if (ImGui::Button("Create Morph from Current Shape"))
        status(session, morph::captureVertexMorph(session, session.deform.captureName),
               "Vertex morph created");
}

void drawBoneTransform(DocumentSession &session) {
    session.deform.mode = DeformMode::pose;
    ImGui::SeparatorText("Bone Transform");
    const auto selectedBones = static_cast<std::size_t>(std::count_if(
        session.selection.items().begin(), session.selection.items().end(),
        [](const auto &item) { return item.kind == SelectionKind::bone; }));
    ImGui::Text("Selected bones: %zu", selectedBones);
    ImGui::TextDisabled("Move and rotate bones in the viewport, then capture the pose as a morph.");
    ImGui::Checkbox("Physics preview", &session.previewPhysics);
    ImGui::Checkbox("IK preview", &session.previewIk);
    ImGui::Separator();
    drawCaptureName(session);
    if (ImGui::Button("Create Bone Morph"))
        status(session, morph::captureBoneMorph(session, session.deform.captureName),
               "Bone morph created");
}

float blendWeight(const PreviewSession &preview, mmd::MorphHandle handle) {
    const auto found = std::find_if(preview.morphValues.begin(), preview.morphValues.end(),
                                    [&](const auto &blend) { return blend.morph == handle; });
    return found == preview.morphValues.end() ? 0.0F : found->weight;
}

void drawMorphMixer(DocumentSession &session) {
    session.deform.mode = DeformMode::inactive;
    ImGui::SeparatorText("Morph Mixer");
    auto &model = session.document.model();
    ImGui::InputText("Search", session.deform.morphSearch.data(), session.deform.morphSearch.size());
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
        if (ImGui::SmallButton(solo ? "Unsolo" : "Solo")) {
            if (solo)
                morph::clearSoloMorph(session);
            else
                morph::setSoloMorph(session, handle);
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Reset Mix"))
        morph::resetMix(session);
    ImGui::SameLine();
    drawCaptureName(session);
    if (ImGui::Button("Create Group Morph from Mix"))
        status(session, morph::captureGroupMorph(session, session.deform.captureName),
               "Group morph created");
    if (ImGui::Button("Bake Mix to Vertex Morph"))
        status(session, morph::bakeMixAsVertexMorph(session, session.deform.captureName),
               "Vertex morph baked");
}

void drawMorphOperations(DocumentSession &session) {
    auto &model = session.document.model();
    if (model.morphs.empty())
        return;
    auto index = currentMorphIndex(session);
    if (morphCombo("Target morph", model, index))
        session.selection.clear();
    session.ui.morphIndex = index;
    const auto source = model.morphs[index];
    const auto sourceHandle = session.document.morphHandle(index);
    ImGui::SeparatorText("Morph Operations");
    ImGui::Text("Target: %s", source.name.c_str());
    ImGui::DragFloat("Scale factor", &session.deform.morphScaleFactor, 0.05F, -10.0F, 10.0F);
    if (ImGui::Button("Scale Delta"))
        status(session, morph::createMorphFromData(session,
                                                   morph::scale(morph::copy(source), session.deform.morphScaleFactor),
                                                   source.name + " Scaled", "Scale Morph"),
               "Scaled morph created");
    ImGui::SameLine();
    if (ImGui::Button("Invert Delta"))
        status(session, morph::createMorphFromData(session, morph::negate(morph::copy(source)),
                                                   source.name + " Inverted", "Invert Morph Delta"),
               "Inverted morph created");
    if (ImGui::Button("Duplicate"))
        status(session, morph::duplicateMorph(session, sourceHandle, source.name + " Copy"),
               "Morph duplicated");
    ImGui::SameLine();
    if (ImGui::Button("Reverse Base and Morph")) {
        const auto references = session.document.referencesTo(sourceHandle);
        const auto referencedByMorph = std::any_of(
            references.begin(), references.end(), [](const auto &reference) {
                return reference.ownerKind == mmd::ReferenceObjectKind::morph;
            });
        const auto result = morph::bakeAndReverseBase(session, sourceHandle);
        status(session, result, "Base and morph reversed");
        if (result.success && referencedByMorph)
            setStatus(session,
                      "Warning: Group/Flip morph references the reversed morph; review the result",
                      UiStatusKind::warning, std::chrono::milliseconds::zero(), true);
    }

    std::size_t otherIndex = morphIndex(session, session.deform.operationMorph).value_or(index);
    if (morphCombo("Second morph", model, otherIndex))
        session.deform.operationMorph = session.document.morphHandle(otherIndex);
    if (ImGui::Button("Combine Morphs")) {
        const auto &other = model.morphs[otherIndex];
        if (source.type != other.type) {
            setStatus(session, "Combine requires morphs of the same type",
                      UiStatusKind::warning);
        } else {
            const std::array<morph::MorphData, 2> values{morph::copy(source), morph::copy(other)};
            status(session, morph::createMorphFromData(session, morph::combine(values),
                                                       source.name + " + " + other.name, "Combine Morphs"),
                   "Combined morph created");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Subtract Morph")) {
        const auto &other = model.morphs[otherIndex];
        if (source.type != other.type) {
            setStatus(session, "Subtract requires morphs of the same type",
                      UiStatusKind::warning);
        } else {
            status(session, morph::createMorphFromData(session,
                                                       morph::subtract(morph::copy(source), morph::copy(other)),
                                                       source.name + " - " + other.name, "Subtract Morph"),
                   "Subtracted morph created");
        }
    }
    if (ImGui::Button("Side Split Left / Right")) {
        if (source.type != 1U) {
            setStatus(session, "Side Split is available for vertex morphs only",
                      UiStatusKind::warning);
        } else {
            const morph::SideSplitOptions options{session.deform.sideSplitCenterX,
                                                   session.deform.sideSplitFeather,
                                                   session.deform.sideSplitSwapSides,
                                                   session.deform.sideSplitDuplicateCenterVertices};
            const auto result = morph::splitSide(model, source, options);
            status(session, morph::createSideSplitMorphs(session, std::move(result.left),
                                                         std::move(result.right), source.name),
                   "Side split morphs created");
        }
    }
    ImGui::DragFloat("Split center X", &session.deform.sideSplitCenterX, 0.001F);
    ImGui::DragFloat("Split feather", &session.deform.sideSplitFeather, 0.001F, 0.0F, 10.0F);
    ImGui::Checkbox("Swap split sides", &session.deform.sideSplitSwapSides);
    ImGui::Checkbox("Duplicate split center", &session.deform.sideSplitDuplicateCenterVertices);
    ImGui::DragInt("Material index", &session.deform.materialIndex, 1.0F, -1,
                   static_cast<int>(model.materials.size()) - 1);
    const auto material = session.deform.materialIndex < 0
                              ? std::nullopt
                              : std::optional<std::size_t>(session.deform.materialIndex);
    const auto filterMaterial = [&](morph::MaterialFilterMode mode, const char *suffix) {
        if (source.type != 1U || !material) {
            setStatus(session, "Select a material and a vertex morph first",
                      UiStatusKind::warning);
            return;
        }
        const auto filtered = morph::filterByMaterial(model, source, material, mode);
        status(session, morph::createMorphFromData(session, filtered,
                                                   source.name + suffix, "Material Mask"),
               "Material-filtered morph created");
    };
    if (ImGui::Button("Exclude Used"))
        filterMaterial(morph::MaterialFilterMode::excludeUsed, " Without Material");
    ImGui::SameLine();
    if (ImGui::Button("Exclude Exclusive"))
        filterMaterial(morph::MaterialFilterMode::excludeExclusive, " Without Exclusive Material");
    if (ImGui::Button("Keep Only Used"))
        filterMaterial(morph::MaterialFilterMode::keepOnlyUsed, " Material Only");
    ImGui::SameLine();
    if (ImGui::Button("Keep Only Exclusive"))
        filterMaterial(morph::MaterialFilterMode::keepOnlyExclusive, " Exclusive Material Only");
}

} // namespace

void drawTransformView(DocumentSession &session, bool *open) {
    if (open != nullptr && !*open) {
        session.deform.engaged = false;
        session.deform.suspended = true;
        session.ui.gizmoDragging = false;
        return;
    }
    if (!ImGui::Begin("Transform View", open)) {
        if (open != nullptr && !*open) {
            session.deform.engaged = false;
            session.deform.suspended = true;
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
    if (session.deform.dirty && ImGui::Button("Discard Temporary Edit")) {
        session.deform.clearOverlay();
        session.deform.mode = session.deform.tab == TransformViewTab::bone
                                  ? DeformMode::pose
                                  : session.deform.tab == TransformViewTab::vertex
                                        ? DeformMode::shape
                                        : DeformMode::inactive;
        refreshDeformPreview(session);
    }
    if (ImGui::RadioButton("Vertex", session.deform.tab == TransformViewTab::vertex)) {
        session.deform.tab = TransformViewTab::vertex;
        session.deform.mode = DeformMode::shape;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Bone", session.deform.tab == TransformViewTab::bone)) {
        session.deform.tab = TransformViewTab::bone;
        session.deform.mode = DeformMode::pose;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Morph", session.deform.tab == TransformViewTab::morph)) {
        session.deform.tab = TransformViewTab::morph;
        session.deform.mode = DeformMode::inactive;
    }
    ImGui::Separator();
    if (session.deform.tab == TransformViewTab::bone)
        drawBoneTransform(session);
    else if (session.deform.tab == TransformViewTab::morph)
        drawMorphMixer(session);
    else
        drawVertexTransform(session);
    drawMorphOperations(session);
    const bool closeRequested = open != nullptr && !*open;
    ImGui::End();
    if (closeRequested) {
        session.deform.engaged = false;
        session.deform.suspended = true;
        session.ui.gizmoDragging = false;
    }
}

} // namespace pmxer
