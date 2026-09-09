#include "ViewportPanel.hpp"
#include "ViewportGizmo.hpp"
#include "ViewportPicking.hpp"
#include "UiSemantics.hpp"

#include "../editor/EditorSelectionController.hpp"
#include "../editor/EditorSelectionQueries.hpp"
#include "../editor/UiStatus.hpp"
#include "../editor/ViewportCapabilities.hpp"
#include "../render/Camera.hpp"
#include "../render/GpuModelRenderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace pmxer {
namespace {

struct Bounds {
    float minX{std::numeric_limits<float>::max()};
    float maxX{std::numeric_limits<float>::lowest()};
    float minY{std::numeric_limits<float>::max()};
    float maxY{std::numeric_limits<float>::lowest()};
    float minZ{std::numeric_limits<float>::max()};
    float maxZ{std::numeric_limits<float>::lowest()};
};

void includePoint(Bounds &bounds, const mmd::Float3 &point) {
    bounds.minX = std::min(bounds.minX, point[0]);
    bounds.maxX = std::max(bounds.maxX, point[0]);
    bounds.minY = std::min(bounds.minY, point[1]);
    bounds.maxY = std::max(bounds.maxY, point[1]);
    bounds.minZ = std::min(bounds.minZ, point[2]);
    bounds.maxZ = std::max(bounds.maxZ, point[2]);
}

[[nodiscard]] bool validBounds(const Bounds &bounds) {
    return bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY &&
           bounds.minZ <= bounds.maxZ;
}

float boundsRadius(const Bounds &bounds) {
    const auto dx = bounds.maxX - bounds.minX;
    const auto dy = bounds.maxY - bounds.minY;
    const auto dz = bounds.maxZ - bounds.minZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5F;
}

void includeVertex(Bounds &bounds, const std::vector<mmd::PmxVertex> &vertices,
                   std::int32_t index) {
    if (index >= 0 && static_cast<std::size_t>(index) < vertices.size())
        includePoint(bounds, vertices[static_cast<std::size_t>(index)].position);
}

void includeFace(Bounds &bounds, const mmd::PmxModel &model, std::size_t face) {
    const auto offset = face * 3U;
    if (offset + 2U >= model.indices.size())
        return;
    for (std::size_t index = 0; index < 3U; ++index)
        includeVertex(bounds, model.vertices,
                      static_cast<std::int32_t>(model.indices[offset + index]));
}

void includeMaterialFaces(Bounds &bounds, const mmd::PmxModel &model,
                          std::size_t materialIndex) {
    std::size_t indexOffset{};
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        const auto faceCount = model.materials[index].indexCount / 3U;
        if (index == materialIndex) {
            const auto firstFace = indexOffset / 3U;
            for (std::size_t face = firstFace; face < firstFace + faceCount; ++face)
                includeFace(bounds, model, face);
            return;
        }
        indexOffset += model.materials[index].indexCount;
    }
}

ImVec2 project(const mmd::Float3 &position, const Bounds &, ImVec2 origin, ImVec2 size, const EditorUiState &ui) {
    const CameraState camera{ui.cameraTarget, ui.cameraYaw, ui.cameraPitch, ui.cameraDistance, ui.orthographic};
    const auto point = projectWorldToScreen(camera, position, origin.x, origin.y, size.x, size.y);
    return {point.x, point.y};
}

ImVec2 project(const mmd::PmxVertex &vertex, const Bounds &bounds, ImVec2 origin, ImVec2 size,
               const EditorUiState &ui) {
    return project(vertex.position, bounds, origin, size, ui);
}

void selectViewportItem(DocumentSession &session, EditorWorkspace &workspace,
                        SelectionItem item, std::size_t index) {
    if (selectMorphOffsetTarget(session, item))
        return;
    if (workspace == EditorWorkspace::morph) {
        setStatus(session, "モーフ本体を選択したまま、オフセット対象pickを開始してください",
                  UiStatusKind::info);
        return;
    }
    if (ImGui::GetIO().KeyCtrl) {
        if (session.selection.contains(item))
            session.selection.remove(item);
        else
            addSelection(session, workspace, item, SelectionOrigin::viewport);
    } else if (ImGui::GetIO().KeyShift) {
        addSelection(session, workspace, item, SelectionOrigin::viewport);
    } else {
        selectPrimary(session, workspace, item, SelectionOrigin::viewport);
    }
    session.ui.clearDrafts();
    if (item.kind == SelectionKind::vertex)
        session.ui.vertexIndex = index;
    else if (item.kind == SelectionKind::material)
        session.ui.materialIndex = index;
    else if (item.kind == SelectionKind::bone)
        session.ui.boneIndex = index;
    else if (item.kind == SelectionKind::rigidBody)
        session.ui.rigidBodyIndex = index;
    else if (item.kind == SelectionKind::joint)
        session.ui.jointIndex = index;
}

std::optional<Bounds> selectionBounds(const DocumentSession &session) {
    if (session.selection.items().empty())
        return std::nullopt;
    const auto &model = session.document.model();
    Bounds bounds;
    std::vector<bool> morphStack(model.morphs.size());
    const auto includeMorph = [&](auto &&self, std::size_t morphIndex) -> void {
        if (morphIndex >= model.morphs.size() || morphStack[morphIndex])
            return;
        morphStack[morphIndex] = true;
        const auto &morph = model.morphs[morphIndex];
        for (const auto &offset : morph.offsets) {
            switch (morph.type) {
            case 0:
            case 9:
                if (offset.index >= 0)
                    self(self, static_cast<std::size_t>(offset.index));
                break;
            case 1:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                includeVertex(bounds, model.vertices, offset.index);
                break;
            case 2:
                if (offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.bones.size())
                    includePoint(bounds, model.bones[static_cast<std::size_t>(offset.index)].position);
                break;
            case 8:
                if (offset.index < 0) {
                    for (std::size_t material = 0; material < model.materials.size(); ++material)
                        includeMaterialFaces(bounds, model, material);
                } else {
                    includeMaterialFaces(bounds, model, static_cast<std::size_t>(offset.index));
                }
                break;
            case 10:
                if (offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.rigidBodies.size()) {
                    const auto &body = model.rigidBodies[static_cast<std::size_t>(offset.index)];
                    const mmd::Float3 half{std::abs(body.size[0]) * 0.5F,
                                           std::abs(body.size[1]) * 0.5F,
                                           std::abs(body.size[2]) * 0.5F};
                    includePoint(bounds, {body.position[0] - half[0], body.position[1] - half[1], body.position[2] - half[2]});
                    includePoint(bounds, {body.position[0] + half[0], body.position[1] + half[1], body.position[2] + half[2]});
                }
                break;
            default:
                break;
            }
        }
        morphStack[morphIndex] = false;
    };
    for (const auto &selected : session.selection.items()) {
        switch (selected.kind) {
        case SelectionKind::vertex: {
            const auto *value = session.document.resolve(selectionHandle<mmd::VertexTag>(session.document, selected));
            if (value != nullptr)
                includePoint(bounds, value->position);
            break;
        }
        case SelectionKind::bone: {
            const auto *value = session.document.resolve(selectionHandle<mmd::BoneTag>(session.document, selected));
            if (value != nullptr)
                includePoint(bounds, value->position);
            break;
        }
        case SelectionKind::rigidBody: {
            const auto *value = session.document.resolve(selectionHandle<mmd::RigidBodyTag>(session.document, selected));
            if (value != nullptr) {
                const mmd::Float3 half{std::abs(value->size[0]) * 0.5F,
                                       std::abs(value->size[1]) * 0.5F,
                                       std::abs(value->size[2]) * 0.5F};
                includePoint(bounds, {value->position[0] - half[0], value->position[1] - half[1], value->position[2] - half[2]});
                includePoint(bounds, {value->position[0] + half[0], value->position[1] + half[1], value->position[2] + half[2]});
            }
            break;
        }
        case SelectionKind::joint: {
            const auto *value = session.document.resolve(selectionHandle<mmd::JointTag>(session.document, selected));
            if (value != nullptr)
                includePoint(bounds, value->position);
            break;
        }
        case SelectionKind::face:
            for (std::size_t index = 0; index < session.document.faces().size(); ++index)
                if (session.document.faceHandle(index) == selectionHandle<mmd::FaceTag>(session.document, selected))
                    includeFace(bounds, model, index);
            break;
        case SelectionKind::material:
            for (std::size_t index = 0; index < model.materials.size(); ++index)
                if (session.document.materialHandle(index) == selectionHandle<mmd::MaterialTag>(session.document, selected))
                    includeMaterialFaces(bounds, model, index);
            break;
        case SelectionKind::texture:
            for (std::size_t texture = 0; texture < model.textures.size(); ++texture) {
                if (session.document.textureHandle(texture) != selectionHandle<mmd::TextureTag>(session.document, selected))
                    continue;
                for (std::size_t material = 0; material < model.materials.size(); ++material)
                    if (model.materials[material].textureIndex == static_cast<std::int32_t>(texture) ||
                        model.materials[material].sphereTextureIndex == static_cast<std::int32_t>(texture) ||
                        model.materials[material].toonTextureIndex == static_cast<std::int32_t>(texture))
                        includeMaterialFaces(bounds, model, material);
            }
            break;
        case SelectionKind::morph: {
            const auto handle = selectionHandle<mmd::MorphTag>(session.document, selected);
            for (std::size_t index = 0; index < model.morphs.size(); ++index) {
                if (session.document.morphHandle(index) == handle) {
                    includeMorph(includeMorph, index);
                    break;
                }
            }
            break;
        }
        case SelectionKind::displayFrame:
        case SelectionKind::softBody:
            break;
        }
    }
    return validBounds(bounds) ? std::optional{bounds} : std::nullopt;
}

void frameBounds(EditorUiState &ui, const Bounds &bounds, float aspect,
                 float modelRadius) {
    if (!validBounds(bounds))
        return;
    ui.cameraTarget = {(bounds.minX + bounds.maxX) * 0.5F,
                       (bounds.minY + bounds.maxY) * 0.5F,
                       (bounds.minZ + bounds.maxZ) * 0.5F};
    const auto radius = std::max({boundsRadius(bounds), modelRadius * 0.01F,
                                  0.001F});
    const auto safeAspect = std::max(aspect, 0.001F);
    const auto verticalHalfFov = 0.75F * 0.5F;
    const auto horizontalHalfFov =
        std::atan(std::tan(verticalHalfFov) * safeAspect);
    const auto limitingHalfFov = std::min(verticalHalfFov, horizontalHalfFov);
    const auto fitDistance = radius / std::sin(limitingHalfFov) * 1.10F;
    ui.cameraDistance = std::max(fitDistance, radius + 0.02F);
    ui.cameraInitialized = true;
}

std::pair<float, float> cameraDistanceLimits(const Bounds &bounds) {
    const auto radius = std::max(boundsRadius(bounds), 0.001F);
    const auto minimum = std::max(radius * 0.02F, 0.001F);
    return {minimum, std::max(radius * 50.0F, minimum * 10.0F)};
}

void setBoneOverlay(DocumentSession &session, WorkspaceViewportProfile *profile,
                    bool value) {
    session.ui.showBones = value;
    if (profile != nullptr)
        profile->showBones = value;
}

void setPhysicsOverlay(DocumentSession &session,
                       WorkspaceViewportProfile *profile, bool value) {
    session.ui.showPhysics = value;
    if (profile != nullptr)
        profile->showPhysics = value;
}

bool isSelected(const DocumentSession &session, SelectionKind kind,
                const auto &handle) {
    return session.selection.contains(
        {kind, handle.domain, handle.id, handle.generation});
}

std::vector<SelectionItem> selectedMaterials(const DocumentSession &session) {
    std::vector<SelectionItem> result;
    for (const auto &item : session.selection.items())
        if (item.kind == SelectionKind::material)
            result.push_back(item);
    return result;
}

void appendUnique(std::vector<SelectionItem> &items,
                  const SelectionItem &item) {
    if (std::find(items.begin(), items.end(), item) == items.end())
        items.push_back(item);
}

void selectRelatedItems(DocumentSession &session, EditorWorkspace &workspace,
                        std::vector<SelectionItem> items) {
    if (items.empty()) {
        setStatus(session, "関連する対象がありません", UiStatusKind::info);
        return;
    }
    selectMany(session, workspace, std::move(items), SelectionOrigin::reference);
    setStatus(session, std::to_string(session.selection.items().size()) +
                         "件を選択しました",
              UiStatusKind::success);
}

void hoverTooltip(const DocumentSession &session, const SelectionItem &item) {
    ImGui::BeginTooltip();
    if (item.kind == SelectionKind::material) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::MaterialTag>(session.document, item));
        ImGui::Text("材質: %s", value == nullptr ? "(無効)" : value->name.c_str());
    } else if (item.kind == SelectionKind::bone) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::BoneTag>(session.document, item));
        if (value == nullptr) {
            ImGui::TextUnformatted("ボーン: (無効)");
        } else {
            ImGui::Text("ボーン: %s", value->name.c_str());
            ImGui::Text("変形層: %d", value->deformLayer);
        }
    } else if (item.kind == SelectionKind::rigidBody) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::RigidBodyTag>(session.document, item));
        ImGui::Text("剛体: %s", value == nullptr ? "(無効)" : value->name.c_str());
    } else if (item.kind == SelectionKind::joint) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::JointTag>(session.document, item));
        ImGui::Text("ジョイント: %s", value == nullptr ? "(無効)" : value->name.c_str());
    } else if (item.kind == SelectionKind::face) {
        ImGui::TextUnformatted("面");
    } else {
        ImGui::TextUnformatted("頂点");
    }
    ImGui::EndTooltip();
}

bool drawViewAxis(EditorUiState &ui, ImDrawList *draw, ImVec2 origin,
                  ImVec2 size) {
    const ImVec2 center{origin.x + size.x - 42.0F, origin.y + 42.0F};
    const std::array<ImVec2, 3> points{{{center.x - 18.0F, center.y + 15.0F},
                                        {center.x + 18.0F, center.y + 15.0F},
                                        {center.x, center.y - 18.0F}}};
    static constexpr const char *labels[]{"X", "Y", "Z"};
    static constexpr ImU32 colors[]{IM_COL32(235, 90, 90, 240),
                                     IM_COL32(100, 215, 120, 240),
                                     IM_COL32(90, 145, 245, 240)};
    const auto mouse = ImGui::GetIO().MousePos;
    bool clicked{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto dx = mouse.x - points[index].x;
        const auto dy = mouse.y - points[index].y;
        const auto hovered = dx * dx + dy * dy <= 12.0F * 12.0F;
        draw->AddCircleFilled(points[index], 12.0F, hovered ? IM_COL32_WHITE : colors[index]);
        const auto textSize = ImGui::CalcTextSize(labels[index]);
        draw->AddText({points[index].x - textSize.x * 0.5F,
                       points[index].y - textSize.y * 0.5F},
                      hovered ? colors[index] : IM_COL32(20, 24, 30, 255), labels[index]);
        if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            continue;
        if (index == 0U) {
            ui.cameraYaw = 1.5707963F;
            ui.cameraPitch = 0.0F;
        } else if (index == 1U) {
            ui.cameraYaw = 0.0F;
            ui.cameraPitch = 1.5697963F;
        } else {
            ui.cameraYaw = 0.0F;
            ui.cameraPitch = 0.0F;
        }
        clicked = true;
    }
    return clicked;
}

} // namespace

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame,
                        GpuModelRenderer *renderer, bool *showDiagnostics,
                        bool *open, EditorWorkspace &activeWorkspace,
                        WorkspaceViewportProfile *profile,
                        ViewportLightingSettings &lighting) {
    session.ui.viewportVisible = false;
    if (!ImGui::Begin("ビューポート", open, ImGuiWindowFlags_NoBackground)) {
        ImGui::End();
        return;
    }
    if (profile != nullptr)
        applyViewportProfile(session, *profile);
    if (session.ui.morphOffsetTarget.picking) {
        if (session.ui.morphOffsetTarget.expectedKind == SelectionKind::bone)
            session.ui.showBones = true;
        if (session.ui.morphOffsetTarget.expectedKind == SelectionKind::rigidBody)
            session.ui.showPhysics = true;
    }
    const auto moveAvailability = actionAvailability(EditorAction::viewportMove, session);
    const auto rotateAvailability = actionAvailability(EditorAction::viewportRotate, session);
    const auto scaleAvailability = actionAvailability(EditorAction::viewportScale, session);
    const auto policy = workspacePolicy(activeWorkspace);
    const auto sharedToonFallbackCount = renderer == nullptr
                                             ? std::size_t{}
                                             : renderer->resourceSummary(session).sharedToonFallbackCount;
    if (renderer != nullptr) {
        const auto resources = renderer->resourceSummary(session);
        if (resources.missingTextureCount != 0U || resources.failedTextureCount != 0U) {
            ImGui::TextColored(ImVec4{1.0F, 0.72F, 0.25F, 1.0F},
                               "⚠ 外部リソース %zu件不足",
                               resources.missingTextureCount + resources.failedTextureCount);
            ImGui::SameLine();
            ImGui::TextDisabled("不足テクスチャを代替表示しています");
            ImGui::SameLine();
            if (ImGui::SmallButton("診断##texture-diagnostics") && showDiagnostics != nullptr)
                               *showDiagnostics = true;
        }
        if (resources.sharedToonFallbackCount != 0U) {
            if (resources.missingTextureCount != 0U || resources.failedTextureCount != 0U)
                ImGui::SameLine();
            ImGui::TextDisabled("共有Toonを簡易表示中");
        }
    }
    const auto targetPicking = session.ui.morphOffsetTarget.picking;
    const auto targetMode = [&] {
        switch (session.ui.morphOffsetTarget.expectedKind) {
        case SelectionKind::vertex:
            return ViewportSelectionMode::vertex;
        case SelectionKind::bone:
            return ViewportSelectionMode::bone;
        case SelectionKind::material:
            return ViewportSelectionMode::material;
        case SelectionKind::rigidBody:
            return ViewportSelectionMode::rigidBody;
        default:
            return ViewportSelectionMode::material;
        }
    }();
    const auto modeButton = [&](const char *label, ViewportSelectionMode mode) {
        const auto supported = policy.allows(mode) ||
                               (targetPicking && mode == targetMode);
        if (!supported)
            return;
        if (ImGui::RadioButton(label, session.ui.selectionMode == mode)) {
            session.ui.selectionMode = mode;
            if (mode == ViewportSelectionMode::bone)
                setBoneOverlay(session, profile, true);
            if (mode == ViewportSelectionMode::rigidBody ||
                mode == ViewportSelectionMode::joint)
                setPhysicsOverlay(session, profile, true);
        }
        ImGui::SameLine();
    };
    const auto normalMorphWorkspace = activeWorkspace == EditorWorkspace::morph &&
                                      !targetPicking;
    if (normalMorphWorkspace) {
        ImGui::TextDisabled("モーフはアウトライナーから選択");
        ImGui::SameLine();
    } else {
        modeButton("頂点", ViewportSelectionMode::vertex);
        modeButton("面", ViewportSelectionMode::face);
        modeButton("材質", ViewportSelectionMode::material);
        modeButton("ボーン", ViewportSelectionMode::bone);
        modeButton("剛体", ViewportSelectionMode::rigidBody);
    }
    const auto jointSupported = policy.allows(ViewportSelectionMode::joint);
    if (jointSupported && ImGui::RadioButton("ジョイント", session.ui.selectionMode == ViewportSelectionMode::joint)) {
        session.ui.selectionMode = ViewportSelectionMode::joint;
        setPhysicsOverlay(session, profile, true);
    }
    if (jointSupported)
        ImGui::SameLine();
    if (targetPicking) {
        const auto targetLabel = session.ui.morphOffsetTarget.expectedKind == SelectionKind::bone
                                     ? "対象選択: ボーン"
                                 : session.ui.morphOffsetTarget.expectedKind == SelectionKind::rigidBody
                                     ? "対象選択: 剛体"
                                     : "対象選択";
        ImGui::TextDisabled("%s", targetLabel);
        ImGui::SameLine();
    }
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ui::checkbox(ui::UiSemanticId::viewportXray, "X-Ray", &session.ui.xray);
    const auto toolButton = [&](const char *label, ViewportTool tool,
                                ui::UiSemanticId semanticId, bool supported,
                                std::string_view tooltip) {
        if (ui::radioButton(semanticId, label,
                            session.ui.viewportTool == tool, supported,
                            supported ? ui::UiSemanticSupport::supported
                                       : ui::UiSemanticSupport::unsupported,
                            tooltip))
            session.ui.viewportTool = tool;
        if (!supported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip.data());
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
    };
    if (ImGui::RadioButton("選択", session.ui.viewportTool == ViewportTool::select))
        session.ui.viewportTool = ViewportTool::select;
    ImGui::SameLine();
    toolButton("移動", ViewportTool::move, ui::UiSemanticId::viewportToolMove,
               moveAvailability.enabled, moveAvailability.reason);
    toolButton("回転", ViewportTool::rotate, ui::UiSemanticId::viewportToolRotate,
               rotateAvailability.enabled, rotateAvailability.reason);
    if (ui::radioButton(ui::UiSemanticId::viewportToolScale, "拡縮",
                        session.ui.viewportTool == ViewportTool::scale,
                        scaleAvailability.enabled,
                        scaleAvailability.enabled ? ui::UiSemanticSupport::supported
                                                  : ui::UiSemanticSupport::unsupported,
                        scaleAvailability.reason))
        session.ui.viewportTool = ViewportTool::scale;
    if (!scaleAvailability.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(scaleAvailability.reason.data());
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("ローカル", &session.ui.localTransform);
    ImGui::SameLine();
    ImGui::Checkbox("スナップ", &session.ui.snapTransform);
    ImGui::SameLine();
    if (ImGui::Button(session.ui.orthographic ? "平行" : "透視"))
        session.ui.orthographic = !session.ui.orthographic;
    ImGui::SameLine();
    if (ui::checkbox(ui::UiSemanticId::viewportShowBones, "ボーン表示",
                     &session.ui.showBones))
        setBoneOverlay(session, profile, session.ui.showBones);
    ImGui::SameLine();
    if (ui::checkbox(ui::UiSemanticId::viewportShowPhysics, "物理表示",
                     &session.ui.showPhysics))
        setPhysicsOverlay(session, profile, session.ui.showPhysics);
    if (profile != nullptr) {
        static constexpr const char *physicsModes[]{"文脈", "全て", "選択のみ"};
        const auto modeIndex = static_cast<std::size_t>(profile->physicsMode);
        ImGui::SameLine();
        const auto &style = ImGui::GetStyle();
        ImGui::SetNextItemWidth(
            ImGui::CalcTextSize("選択のみ").x + ImGui::GetFrameHeight() +
            style.FramePadding.x * 2.0F);
        if (ImGui::BeginCombo("##physics-overlay-mode",
                              physicsModes[std::min(modeIndex, std::size(physicsModes) - 1U)])) {
            for (std::size_t index = 0; index < std::size(physicsModes); ++index) {
                if (ImGui::Selectable(physicsModes[index], index == modeIndex))
                    profile->physicsMode = static_cast<PhysicsOverlayMode>(index);
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    const auto shadingLabel = [&] {
        switch (lighting.mode) {
        case ViewportShadingMode::mmd:
            return sharedToonFallbackCount != 0U ? "MMD*" : "MMD";
        case ViewportShadingMode::neutral:
            return "Neutral";
        case ViewportShadingMode::unlit:
            return "Unlit";
        }
        return "表示";
    }();
    if (ImGui::Button("表示##viewport-display"))
        ImGui::OpenPopup("viewport-display-settings");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("ビューポート表示");
        ImGui::Text("現在: %s", shadingLabel);
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopup("viewport-display-settings")) {
        ImGui::SeparatorText("シェーディング");
        if (ImGui::RadioButton("MMD", lighting.mode == ViewportShadingMode::mmd))
            applyViewportShadingPreset(lighting, ViewportShadingMode::mmd);
        ImGui::SameLine();
        if (ImGui::RadioButton("Neutral", lighting.mode == ViewportShadingMode::neutral))
            applyViewportShadingPreset(lighting, ViewportShadingMode::neutral);
        ImGui::SameLine();
        if (ImGui::RadioButton("Unlit", lighting.mode == ViewportShadingMode::unlit))
            applyViewportShadingPreset(lighting, ViewportShadingMode::unlit);

        if (renderer != nullptr) {
            if (sharedToonFallbackCount != 0U)
                ImGui::TextColored(ImVec4{1.0F, 0.72F, 0.25F, 1.0F},
                                   "⚠ 共有Toonを簡易表示中 (%zu件)",
                                   sharedToonFallbackCount);
        }

        const auto unlit = lighting.mode == ViewportShadingMode::unlit;
        const auto mmd = lighting.mode == ViewportShadingMode::mmd;
        ImGui::SeparatorText("照明");
        if (unlit)
            ImGui::BeginDisabled();
        ImGui::SliderAngle("方位", &lighting.lightYaw, -180.0F, 180.0F);
        ImGui::SliderAngle("高さ", &lighting.lightPitch, -89.0F, 89.0F);
        ImGui::SliderFloat("強さ", &lighting.lightIntensity, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("環境光", &lighting.ambientIntensity, 0.0F, 1.0F, "%.2f");
        if (unlit)
            ImGui::EndDisabled();

        ImGui::SeparatorText("表示");
        ImGui::SliderFloat("Exposure", &lighting.exposure, -3.0F, 3.0F, "%+.2f EV");
        if (unlit || mmd)
            ImGui::BeginDisabled();
        ImGui::SliderFloat("Toon強度", &lighting.toonStrength, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Specular", &lighting.specularStrength, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Sphere", &lighting.sphereStrength, 0.0F, 1.0F, "%.2f");
        if (unlit || mmd)
            ImGui::EndDisabled();

        ImGui::SeparatorText("背景");
        ImGui::ColorEdit3("背景色", lighting.background.data());
        if (ImGui::Button("初期値に戻す"))
            lighting = ViewportLightingSettings{};
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("?");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("右ドラッグ: 回転");
        ImGui::TextUnformatted("ホイール: ズーム");
        ImGui::TextUnformatted("Shift + 中ドラッグ: パン");
        ImGui::TextUnformatted("Ctrl + 中ドラッグ: ドリー");
        ImGui::TextUnformatted("F: 選択へフォーカス / Home: 全体表示");
        ImGui::EndTooltip();
    }
    const auto available = ImGui::GetContentRegionAvail();
    if (available.x < 10.0F || available.y < 10.0F) {
        ImGui::End();
        return;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    session.ui.viewportX = origin.x;
    session.ui.viewportY = origin.y;
    session.ui.viewportWidth = available.x;
    session.ui.viewportHeight = available.y;
    session.ui.viewportVisible = true;
    ImGui::InvisibleButton("viewport-canvas", available);
    const auto hovered = ImGui::IsItemHovered();
    const auto mouse = ImGui::GetIO().MousePos;
    if (session.ui.viewportTool == ViewportTool::select && ImGui::IsItemActivated()) {
        session.ui.boxSelecting = true;
        session.ui.boxSelectStartX = mouse.x;
        session.ui.boxSelectStartY = mouse.y;
        session.ui.boxSelectEndX = mouse.x;
        session.ui.boxSelectEndY = mouse.y;
    }
    if (session.ui.boxSelecting) {
        session.ui.boxSelectEndX = mouse.x;
        session.ui.boxSelectEndY = mouse.y;
    }
    auto *draw = ImGui::GetWindowDrawList();

    const auto &model = session.document.model();
    const auto &vertices = frame != nullptr && !frame->vertices.empty() ? frame->vertices : model.vertices;
    if (vertices.empty()) {
        draw->AddText({origin.x + 16.0F, origin.y + 16.0F}, IM_COL32_WHITE, "頂点がありません");
        ImGui::End();
        return;
    }
    if (session.ui.viewportBoundsRevision != session.revision) {
        Bounds updated;
        for (const auto &vertex : model.vertices) {
            updated.minX = std::min(updated.minX, vertex.position[0]);
            updated.maxX = std::max(updated.maxX, vertex.position[0]);
            updated.minY = std::min(updated.minY, vertex.position[1]);
            updated.maxY = std::max(updated.maxY, vertex.position[1]);
            updated.minZ = std::min(updated.minZ, vertex.position[2]);
            updated.maxZ = std::max(updated.maxZ, vertex.position[2]);
        }
        session.ui.viewportBoundsMin = {updated.minX, updated.minY, updated.minZ};
        session.ui.viewportBoundsMax = {updated.maxX, updated.maxY, updated.maxZ};
        session.ui.viewportBoundsRevision = session.revision;
    }
    const Bounds bounds{session.ui.viewportBoundsMin[0], session.ui.viewportBoundsMax[0],
                        session.ui.viewportBoundsMin[1], session.ui.viewportBoundsMax[1],
                        session.ui.viewportBoundsMin[2], session.ui.viewportBoundsMax[2]};
    if (!session.ui.cameraInitialized) {
        frameBounds(session.ui, bounds,
                    available.x / std::max(available.y, 1.0F),
                    boundsRadius(bounds));
    }
    const auto [minimumDistance, maximumDistance] = cameraDistanceLimits(bounds);
    CameraState camera{session.ui.cameraTarget, session.ui.cameraYaw,
                       session.ui.cameraPitch, session.ui.cameraDistance,
                       session.ui.orthographic};
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        session.ui.cameraOrbiting = true;
        session.ui.cameraOrbitMoved = false;
        session.ui.cameraNavigationStartX = mouse.x;
        session.ui.cameraNavigationStartY = mouse.y;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        session.ui.cameraPanning = ImGui::GetIO().KeyShift;
        session.ui.cameraDollying = ImGui::GetIO().KeyCtrl;
        if (session.ui.cameraPanning || session.ui.cameraDollying) {
            session.ui.cameraNavigationStartX = mouse.x;
            session.ui.cameraNavigationStartY = mouse.y;
        }
    }
    if (session.ui.cameraOrbiting) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            const auto dx = mouse.x - session.ui.cameraNavigationStartX;
            const auto dy = mouse.y - session.ui.cameraNavigationStartY;
            if (!session.ui.cameraOrbitMoved && dx * dx + dy * dy > 16.0F)
                session.ui.cameraOrbitMoved = true;
            if (session.ui.cameraOrbitMoved) {
                const auto delta = ImGui::GetIO().MouseDelta;
                session.ui.cameraYaw += delta.x * 0.008F;
                session.ui.cameraPitch = std::clamp(
                    session.ui.cameraPitch + delta.y * 0.008F, -1.5F, 1.5F);
            }
        } else {
            if (!session.ui.cameraOrbitMoved) {
                const ImVec2 releasePosition{
                    session.ui.cameraNavigationStartX,
                    session.ui.cameraNavigationStartY};
                const auto picked = pickViewport(
                    session, vertices, camera, origin, available,
                    releasePosition);
                if (picked) {
                    if (!session.selection.contains(picked->item))
                        selectViewportItem(session, activeWorkspace, picked->item, picked->index);
                    session.ui.viewportHover = picked->item;
                    session.ui.viewportHoverFace = picked->face;
                    session.ui.viewportHoverPosition = picked->position;
                    ImGui::OpenPopup("viewport-context");
                }
            }
            session.ui.cameraOrbiting = false;
            session.ui.cameraOrbitMoved = false;
        }
    }
    if (session.ui.cameraPanning || session.ui.cameraDollying) {
        const auto buttonDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (buttonDown) {
            const auto dx = mouse.x - session.ui.cameraNavigationStartX;
            const auto dy = mouse.y - session.ui.cameraNavigationStartY;
            if (dx * dx + dy * dy > 16.0F) {
                const auto delta = ImGui::GetIO().MouseDelta;
                if (session.ui.cameraDollying) {
                    session.ui.cameraDistance = std::clamp(
                        session.ui.cameraDistance * std::exp(delta.y * 0.01F),
                        minimumDistance, maximumDistance);
                } else {
                    const auto scale = session.ui.cameraDistance * 0.0015F;
                    session.ui.cameraTarget[0] -=
                        delta.x * std::cos(session.ui.cameraYaw) * scale;
                    session.ui.cameraTarget[2] +=
                        delta.x * std::sin(session.ui.cameraYaw) * scale;
                    session.ui.cameraTarget[1] += delta.y * scale;
                }
            }
        } else {
            session.ui.cameraPanning = false;
            session.ui.cameraDollying = false;
        }
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0F)
        session.ui.cameraDistance = std::clamp(session.ui.cameraDistance *
                                                   std::exp(-ImGui::GetIO().MouseWheel * 0.12F),
                                               minimumDistance, maximumDistance);
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F)) {
        if (const auto selected = selectionBounds(session))
            frameBounds(session.ui, *selected,
                        available.x / std::max(available.y, 1.0F),
                        boundsRadius(bounds));
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Home))
        frameBounds(session.ui, bounds,
                    available.x / std::max(available.y, 1.0F),
                    boundsRadius(bounds));
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Keypad1)) {
        session.ui.cameraYaw = ImGui::GetIO().KeyCtrl ? 3.1415926F : 0.0F;
        session.ui.cameraPitch = 0.0F;
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Keypad3)) {
        session.ui.cameraYaw = ImGui::GetIO().KeyCtrl ? -1.5707963F : 1.5707963F;
        session.ui.cameraPitch = 0.0F;
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Keypad7)) {
        session.ui.cameraYaw = 0.0F;
        session.ui.cameraPitch = ImGui::GetIO().KeyCtrl ? -1.5697963F : 1.5697963F;
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Keypad5))
        session.ui.orthographic = !session.ui.orthographic;
    if (!ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (session.ui.morphOffsetTarget.picking) {
            session.ui.morphOffsetTarget = {};
            setStatus(session, "モーフオフセット対象の選択をキャンセルしました",
                      UiStatusKind::info);
        } else if (hovered) {
            session.ui.viewportTool = ViewportTool::select;
        }
    }
    if (hovered && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1) && policy.allows(ViewportSelectionMode::vertex))
            session.ui.selectionMode = ViewportSelectionMode::vertex;
        if (ImGui::IsKeyPressed(ImGuiKey_2) && policy.allows(ViewportSelectionMode::face))
            session.ui.selectionMode = ViewportSelectionMode::face;
        if (ImGui::IsKeyPressed(ImGuiKey_3) && policy.allows(ViewportSelectionMode::material))
            session.ui.selectionMode = ViewportSelectionMode::material;
        if (ImGui::IsKeyPressed(ImGuiKey_4) && policy.allows(ViewportSelectionMode::bone)) {
            session.ui.selectionMode = ViewportSelectionMode::bone;
            setBoneOverlay(session, profile, true);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_5) && policy.allows(ViewportSelectionMode::rigidBody)) {
            session.ui.selectionMode = ViewportSelectionMode::rigidBody;
            setPhysicsOverlay(session, profile, true);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_6) && policy.allows(ViewportSelectionMode::joint)) {
            session.ui.selectionMode = ViewportSelectionMode::joint;
            setPhysicsOverlay(session, profile, true);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            if (moveAvailability.enabled)
                session.ui.viewportTool = ViewportTool::move;
            else
                setStatus(session, std::string(moveAvailability.reason),
                          UiStatusKind::warning);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            if (rotateAvailability.enabled)
                session.ui.viewportTool = ViewportTool::rotate;
            else
                setStatus(session, std::string(rotateAvailability.reason),
                          UiStatusKind::warning);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (scaleAvailability.enabled)
                session.ui.viewportTool = ViewportTool::scale;
            else
                setStatus(session, std::string(scaleAvailability.reason),
                          UiStatusKind::warning);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            if (activeWorkspace == EditorWorkspace::morph &&
                session.ui.morphOffsetTarget.picking) {
            setStatus(session, "モーフオフセット対象を選択中です",
                      UiStatusKind::info);
            } else if (ImGui::GetIO().KeyAlt)
                session.selection.clear();
            else if (!selectAllForMode(session, activeWorkspace,
                                       session.ui.selectionMode,
                                       SelectionOrigin::viewport))
            setStatus(session, "このワークスペースでは一括選択できません",
                      UiStatusKind::warning);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyAlt)
            session.ui.xray = !session.ui.xray;
        if (ImGui::IsKeyPressed(ImGuiKey_H)) {
            if (ImGui::GetIO().KeyAlt) {
                session.ui.hiddenMaterials.clear();
                session.ui.isolatedMaterials.clear();
            } else {
                const auto materials = selectedMaterials(session);
                if (ImGui::GetIO().KeyShift) {
                    session.ui.hiddenMaterials.clear();
                    session.ui.isolatedMaterials = materials;
                } else {
                    for (const auto &item : materials)
                        appendUnique(session.ui.hiddenMaterials, item);
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Slash)) {
            session.ui.hiddenMaterials.clear();
            session.ui.isolatedMaterials = selectedMaterials(session);
        }
    }

    if (session.ui.showBones) {
        for (std::size_t i = 0; i < model.bones.size(); ++i) {
            const auto &bone = model.bones[i];
            const auto handle = session.document.boneHandle(i);
            const auto selected = isSelected(session, SelectionKind::bone, handle);
            const auto point = project(bone.position, bounds, origin, available,
                                       session.ui);
            draw->AddCircleFilled(point, selected ? 5.0F : 3.0F,
                                  selected ? IM_COL32(255, 225, 90, 255)
                                           : IM_COL32(245, 190, 80,
                                                      static_cast<int>(255.0F * (profile != nullptr ? profile->boneOpacity : 0.8F))));
            if (bone.parent < 0 ||
                static_cast<std::size_t>(bone.parent) >= model.bones.size())
                continue;
            draw->AddLine(
                point,
                project(model.bones[static_cast<std::size_t>(bone.parent)].position,
                        bounds, origin, available, session.ui),
                selected ? IM_COL32(255, 225, 90, 255)
                         : IM_COL32(245, 190, 80,
                                    static_cast<int>(255.0F * (profile != nullptr ? profile->boneOpacity : 0.8F))),
                selected ? 3.0F : 2.0F);
        }
    }
    if (session.ui.showPhysics) {
        const auto relations = physicsSelectionRelations(session);
        const auto opacityFor = [&](bool selected, bool related) {
            const auto mode = profile == nullptr
                                  ? PhysicsOverlayMode::context
                                  : profile->physicsMode;
            if (mode == PhysicsOverlayMode::selectedOnly)
                return selected ? 1.0F : 0.0F;
            if (mode == PhysicsOverlayMode::all)
                return selected ? 1.0F : (profile == nullptr ? 0.25F : profile->physicsOpacity);
            return selected ? 1.0F : (related ? 0.65F : (profile == nullptr ? 0.2F : profile->physicsOpacity));
        };
        for (std::size_t index = 0; index < model.rigidBodies.size(); ++index) {
            const auto &body = model.rigidBodies[index];
            const auto point = project(body.position, bounds, origin, available,
                                       session.ui);
            const auto radius = std::max(3.0F, (std::abs(body.size[0]) + std::abs(body.size[1])) * 0.5F *
                                                   std::min(available.x, available.y) /
                                                   std::max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY));
            const auto handle = session.document.rigidBodyHandle(index);
            const auto selected = relations.bodySelected(handle.id);
            const auto opacity = opacityFor(selected, relations.bodyRelated(handle.id));
            if (opacity <= 0.0F)
                continue;
            const auto color = selected ? IM_COL32(255, 225, 90, 255)
                                        : IM_COL32(180, 230, 255,
                                                   static_cast<int>(255.0F * opacity));
            if (body.shape == 1)
                draw->AddRect({point.x - radius, point.y - radius}, {point.x + radius, point.y + radius},
                              color, 0.0F, ImDrawFlags_None, selected ? 3.0F : 1.0F);
            else
                draw->AddCircle(point, radius, color, 0, selected ? 3.0F : 1.0F);
        }
        for (std::size_t index = 0; index < model.joints.size(); ++index) {
            const auto &joint = model.joints[index];
            if (joint.bodyA < 0 || joint.bodyB < 0 ||
                static_cast<std::size_t>(joint.bodyA) >= model.rigidBodies.size() ||
                static_cast<std::size_t>(joint.bodyB) >= model.rigidBodies.size())
                continue;
            const auto handle = session.document.jointHandle(index);
            const auto selected = relations.jointSelected(handle.id);
            const auto opacity = opacityFor(selected, relations.jointRelated(handle.id));
            if (opacity <= 0.0F)
                continue;
            const auto color = selected ? IM_COL32(255, 225, 90, 255)
                                        : IM_COL32(180, 255, 180,
                                                   static_cast<int>(255.0F * opacity));
            draw->AddLine(
                project(model.rigidBodies[static_cast<std::size_t>(joint.bodyA)].position,
                        bounds, origin, available, session.ui),
                project(model.rigidBodies[static_cast<std::size_t>(joint.bodyB)].position,
                        bounds, origin, available, session.ui),
                color, selected ? 3.0F : 1.0F);
            draw->AddCircleFilled(project(joint.position, bounds, origin, available,
                                          session.ui),
                                  selected ? 5.0F : 3.0F, color);
        }
    }
    for (const auto &selected : session.selection.items()) {
        if (selected.kind != SelectionKind::vertex)
            continue;
        const auto *vertex = session.document.resolve(
            selectionHandle<mmd::VertexTag>(session.document, selected));
        if (vertex != nullptr)
            draw->AddCircleFilled(project(vertex->position, bounds, origin,
                                          available, session.ui),
                                  4.0F, IM_COL32(255, 180, 60, 255));
    }
    if (!session.selection.items().empty() && session.selection.items().front().kind == SelectionKind::vertex) {
        const auto selected = session.selection.items().front();
        for (std::size_t i = 0; i < model.vertices.size(); ++i) {
            const auto handle = session.document.vertexHandle(i);
            if (handle.domain != selected.domain || handle.id != selected.id ||
                handle.generation != selected.generation ||
                model.vertices[i].weightType != mmd::PmxWeightType::sdef)
                continue;
            for (const auto &[value, color, radius] :
                 std::array<std::tuple<mmd::Float3, ImU32, float>, 3>{{{model.vertices[i].sdefC, IM_COL32(255, 100, 100, 255), 5.0F},
                                                                        {model.vertices[i].sdefR0, IM_COL32(100, 255, 100, 255), 4.0F},
                                                                        {model.vertices[i].sdefR1, IM_COL32(100, 150, 255, 255), 4.0F}}}) {
                mmd::PmxVertex marker;
                marker.position = value;
                draw->AddCircleFilled(project(marker, bounds, origin, available, session.ui), radius, color);
            }
        }
    }
    if (!session.selection.items().empty() && session.selection.items().front().kind == SelectionKind::morph) {
        const auto selected = session.selection.items().front();
        for (std::size_t i = 0; i < model.morphs.size(); ++i) {
            const auto handle = session.document.morphHandle(i);
            if (handle.domain != selected.domain || handle.id != selected.id ||
                handle.generation != selected.generation)
                continue;
            const auto &morph = model.morphs[i];
            if (morph.type != 1 && (morph.type < 3 || morph.type > 7))
                continue;
            for (const auto &offset : morph.offsets)
                if (offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.vertices.size()) {
                    draw->AddCircle(project(model.vertices[static_cast<std::size_t>(offset.index)], bounds, origin, available,
                                             session.ui),
                                    5.0F, IM_COL32(220, 100, 255, 240));
                }
        }
    }
    camera = CameraState{session.ui.cameraTarget, session.ui.cameraYaw,
                         session.ui.cameraPitch, session.ui.cameraDistance,
                         session.ui.orthographic};
    if (drawViewAxis(session.ui, draw, origin, available))
        session.ui.boxSelecting = false;
    if (session.ui.boxSelecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImVec2 start{session.ui.boxSelectStartX, session.ui.boxSelectStartY};
        const ImVec2 end{session.ui.boxSelectEndX, session.ui.boxSelectEndY};
        const auto dx = end.x - start.x;
        const auto dy = end.y - start.y;
        if (dx * dx + dy * dy > 16.0F) {
            const auto picked = pickViewportRectangle(session, vertices, camera, origin,
                                                      available, start, end);
            const auto targetCaptured = session.ui.morphOffsetTarget.picking && !picked.empty();
            if (targetCaptured) {
                selectViewportItem(session, activeWorkspace, picked.front().item,
                                   picked.front().index);
            }
            if (!targetCaptured && activeWorkspace != EditorWorkspace::morph) {
                std::vector<SelectionItem> selected;
                selected.reserve(picked.size());
                for (const auto &item : picked)
                    selected.push_back(item.item);
                if (ImGui::GetIO().KeyCtrl) {
                    for (const auto &item : selected)
                        toggleSelection(session, activeWorkspace, item,
                                        SelectionOrigin::viewport);
                } else if (ImGui::GetIO().KeyShift) {
                    for (const auto &item : selected)
                        addSelection(session, activeWorkspace, item,
                                     SelectionOrigin::viewport);
                } else if (!selected.empty()) {
                    selectPrimary(session, activeWorkspace, selected.front(),
                                  SelectionOrigin::viewport);
                    for (std::size_t index = 1; index < selected.size(); ++index)
                        addSelection(session, activeWorkspace, selected[index],
                                     SelectionOrigin::viewport);
                }
                if (!picked.empty()) {
                    const auto &first = picked.front();
                    session.ui.clearDrafts();
                    if (first.item.kind == SelectionKind::vertex)
                        session.ui.vertexIndex = first.index;
                    else if (first.item.kind == SelectionKind::material)
                        session.ui.materialIndex = first.index;
                    else if (first.item.kind == SelectionKind::bone)
                        session.ui.boneIndex = first.index;
                    else if (first.item.kind == SelectionKind::rigidBody)
                        session.ui.rigidBodyIndex = first.index;
                    else if (first.item.kind == SelectionKind::joint)
                        session.ui.jointIndex = first.index;
                }
            } else if (!targetCaptured && activeWorkspace == EditorWorkspace::morph) {
                setStatus(session, "モーフ本体を選択したまま、オフセット対象pickを開始してください",
                          UiStatusKind::info);
            }
        } else {
            const auto picked = pickViewport(session, vertices, camera, origin,
                                             available, end);
            if (picked)
                selectViewportItem(session, activeWorkspace, picked->item, picked->index);
            else if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
                     activeWorkspace != EditorWorkspace::morph &&
                     !session.ui.morphOffsetTarget.picking)
                session.selection.clear();
        }
        session.ui.boxSelecting = false;
    }
    const auto cameraNavigating = session.ui.cameraOrbiting ||
                                  session.ui.cameraPanning ||
                                  session.ui.cameraDollying;
    if (!hovered || session.ui.gizmoDragging || cameraNavigating) {
        session.ui.viewportHover.reset();
        session.ui.viewportHoverFace.reset();
    } else {
        const auto now = std::chrono::steady_clock::now();
        const auto dx = mouse.x - session.ui.viewportHoverMouseX;
        const auto dy = mouse.y - session.ui.viewportHoverMouseY;
        const auto moved = dx * dx + dy * dy > 1.0F;
        const auto stale =
            session.ui.viewportHoverMode != session.ui.selectionMode ||
            session.ui.viewportHoverRevision != session.revision ||
            session.ui.viewportHoverFrameRevision !=
                session.preview.frameRevision;
        if ((moved || stale) &&
            now - session.ui.viewportHoverUpdated >=
                std::chrono::milliseconds(75)) {
            const auto picked = pickViewport(session, vertices, camera, origin,
                                             available, mouse);
            session.ui.viewportHover =
                picked ? std::optional<SelectionItem>{picked->item}
                       : std::nullopt;
            session.ui.viewportHoverFace =
                picked ? picked->face : std::nullopt;
            if (picked)
                session.ui.viewportHoverPosition = picked->position;
            session.ui.viewportHoverMouseX = mouse.x;
            session.ui.viewportHoverMouseY = mouse.y;
            session.ui.viewportHoverMode = session.ui.selectionMode;
            session.ui.viewportHoverRevision = session.revision;
            session.ui.viewportHoverFrameRevision =
                session.preview.frameRevision;
            session.ui.viewportHoverUpdated = now;
        }
    }
    if (session.ui.viewportHover) {
        if (session.ui.viewportHoverFace) {
            const auto offset = *session.ui.viewportHoverFace * 3U;
            if (offset + 2U < model.indices.size()) {
                const auto first = static_cast<std::size_t>(model.indices[offset]);
                const auto second =
                    static_cast<std::size_t>(model.indices[offset + 1U]);
                const auto third =
                    static_cast<std::size_t>(model.indices[offset + 2U]);
                if (first < vertices.size() && second < vertices.size() &&
                    third < vertices.size()) {
                    const auto a = project(vertices[first], bounds, origin,
                                           available, session.ui);
                    const auto b = project(vertices[second], bounds, origin,
                                           available, session.ui);
                    const auto c = project(vertices[third], bounds, origin,
                                           available, session.ui);
                    draw->AddTriangle(a, b, c, IM_COL32(255, 255, 255, 245),
                                      2.0F);
                }
            }
        } else {
            draw->AddCircle(project(session.ui.viewportHoverPosition, bounds,
                                    origin, available, session.ui),
                            7.0F, IM_COL32(255, 255, 255, 245), 0, 2.0F);
        }
        hoverTooltip(session, *session.ui.viewportHover);
    }
    if (session.ui.boxSelecting) {
        const ImVec2 start{session.ui.boxSelectStartX, session.ui.boxSelectStartY};
        const ImVec2 end{session.ui.boxSelectEndX, session.ui.boxSelectEndY};
        if (std::abs(end.x - start.x) > 2.0F || std::abs(end.y - start.y) > 2.0F) {
            const ImVec2 minimum{std::min(start.x, end.x), std::min(start.y, end.y)};
            const ImVec2 maximum{std::max(start.x, end.x), std::max(start.y, end.y)};
            draw->AddRectFilled(minimum, maximum, IM_COL32(80, 150, 255, 35));
            draw->AddRect(minimum, maximum, IM_COL32(110, 180, 255, 230));
        }
    }
    if (ImGui::BeginPopup("viewport-context")) {
        if (ImGui::MenuItem("選択対象へフォーカス")) {
            if (const auto selected = selectionBounds(session))
                frameBounds(session.ui, *selected,
                            available.x / std::max(available.y, 1.0F),
                            boundsRadius(bounds));
            else {
                Bounds hoverBounds;
                includePoint(hoverBounds, session.ui.viewportHoverPosition);
                frameBounds(session.ui, hoverBounds,
                            available.x / std::max(available.y, 1.0F),
                            boundsRadius(bounds));
            }
        }
        const auto materials = selectedMaterials(session);
        if (!materials.empty()) {
            ImGui::Separator();
            if (ImGui::MenuItem("選択材質を隠す")) {
                for (const auto &item : materials)
                    appendUnique(session.ui.hiddenMaterials, item);
                setStatus(session, "選択材質を非表示にしました",
                          UiStatusKind::success);
            }
            if (ImGui::MenuItem("選択材質だけ表示")) {
                session.ui.hiddenMaterials.clear();
                session.ui.isolatedMaterials = materials;
            setStatus(session, "選択材質を分離表示しました",
                      UiStatusKind::success);
            }
            const auto *material = session.document.resolve(
                selectionHandle<mmd::MaterialTag>(session.document,
                                                   materials.front()));
            if (material != nullptr && material->textureIndex >= 0 &&
                static_cast<std::size_t>(material->textureIndex) <
                    model.textures.size() &&
                ImGui::MenuItem("テクスチャへ移動")) {
                const auto index =
                    static_cast<std::size_t>(material->textureIndex);
                const auto texture = session.document.textureHandle(index);
                selectPrimary(session, activeWorkspace,
                              {SelectionKind::texture, texture.domain,
                               texture.id, texture.generation},
                              SelectionOrigin::reference);
                session.ui.textureIndex = index;
                session.ui.clearDrafts();
            }
        }
        if (!session.selection.items().empty()) {
            const auto selected = session.selection.items().front();
            if (selected.kind == SelectionKind::bone) {
                if (ImGui::MenuItem("子ボーンを選択"))
                    selectRelatedItems(session, activeWorkspace,
                                       childBones(session, selected));
                if (ImGui::MenuItem("ウェイト頂点を選択"))
                    selectRelatedItems(session, activeWorkspace,
                                       weightedVertices(session, selected));
            } else if (selected.kind == SelectionKind::material) {
                if (ImGui::MenuItem("面を選択"))
                    selectRelatedItems(session, activeWorkspace,
                                       facesForMaterial(session, selected));
                if (ImGui::MenuItem("頂点を選択"))
                    selectRelatedItems(session, activeWorkspace,
                                       verticesForMaterial(session, selected));
            } else if (selected.kind == SelectionKind::rigidBody) {
                if (ImGui::MenuItem("接続ジョイントを選択"))
                    selectRelatedItems(session, activeWorkspace,
                                       jointsForRigidBody(session, selected));
            } else if (selected.kind == SelectionKind::joint) {
                if (ImGui::MenuItem("接続剛体を選択"))
                    selectRelatedItems(session, activeWorkspace,
                                       rigidBodiesForJoint(session, selected));
            }
        }
        if ((!session.ui.hiddenMaterials.empty() ||
             !session.ui.isolatedMaterials.empty()) &&
            ImGui::MenuItem("すべて表示")) {
            session.ui.hiddenMaterials.clear();
            session.ui.isolatedMaterials.clear();
            setStatus(session, "すべての材質を表示しました",
                      UiStatusKind::success);
        }
        ImGui::EndPopup();
    }
    drawViewportGizmo(session, makeCameraMatrices(camera, available.x / available.y), origin, available);
    ImGui::End();
}

} // namespace pmxer
