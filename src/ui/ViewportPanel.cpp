#include "ViewportPanel.hpp"
#include "ViewportGizmo.hpp"
#include "ViewportPicking.hpp"
#include "UiAutomation.hpp"

#include "../render/Camera.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
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

ImVec2 project(const mmd::Float3 &position, const Bounds &, ImVec2 origin, ImVec2 size, const EditorUiState &ui) {
    const CameraState camera{ui.cameraTarget, ui.cameraYaw, ui.cameraPitch, ui.cameraDistance, ui.orthographic};
    const auto point = projectWorldToScreen(camera, position, origin.x, origin.y, size.x, size.y);
    return {point.x, point.y};
}

ImVec2 project(const mmd::PmxVertex &vertex, const Bounds &bounds, ImVec2 origin, ImVec2 size,
               const EditorUiState &ui) {
    return project(vertex.position, bounds, origin, size, ui);
}

void selectViewportItem(DocumentSession &session, SelectionItem item, std::size_t index) {
    if (ImGui::GetIO().KeyCtrl) {
        if (session.selection.contains(item))
            session.selection.remove(item);
        else
            session.selection.add(item);
    } else if (ImGui::GetIO().KeyShift) {
        session.selection.add(item);
    } else {
        session.selection.set(item);
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

std::optional<mmd::Float3> selectedPosition(const DocumentSession &session) {
    if (session.selection.items().empty())
        return std::nullopt;
    const auto selected = session.selection.items().front();
    if (selected.kind == SelectionKind::vertex) {
        if (const auto *value = session.document.resolve(selectionHandle<mmd::VertexTag>(session.document, selected)))
            return value->position;
    } else if (selected.kind == SelectionKind::bone) {
        if (const auto *value = session.document.resolve(selectionHandle<mmd::BoneTag>(session.document, selected)))
            return value->position;
    } else if (selected.kind == SelectionKind::rigidBody) {
        if (const auto *value = session.document.resolve(selectionHandle<mmd::RigidBodyTag>(session.document, selected)))
            return value->position;
    } else if (selected.kind == SelectionKind::joint) {
        if (const auto *value = session.document.resolve(selectionHandle<mmd::JointTag>(session.document, selected)))
            return value->position;
    }
    return std::nullopt;
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

std::vector<SelectionItem> allItemsForMode(const DocumentSession &session) {
    std::vector<SelectionItem> result;
    const auto append = [&](SelectionKind kind, std::size_t count, const auto &handleAt) {
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto handle = handleAt(index);
            result.push_back({kind, handle.domain, handle.id, handle.generation});
        }
    };
    const auto &model = session.document.model();
    switch (session.ui.selectionMode) {
    case ViewportSelectionMode::vertex:
        append(SelectionKind::vertex, model.vertices.size(),
               [&](std::size_t index) { return session.document.vertexHandle(index); });
        break;
    case ViewportSelectionMode::face:
        append(SelectionKind::face, model.indices.size() / 3U,
               [&](std::size_t index) { return session.document.faceHandle(index); });
        break;
    case ViewportSelectionMode::material:
        append(SelectionKind::material, model.materials.size(),
               [&](std::size_t index) { return session.document.materialHandle(index); });
        break;
    case ViewportSelectionMode::bone:
        append(SelectionKind::bone, model.bones.size(),
               [&](std::size_t index) { return session.document.boneHandle(index); });
        break;
    case ViewportSelectionMode::rigidBody:
        append(SelectionKind::rigidBody, model.rigidBodies.size(),
               [&](std::size_t index) { return session.document.rigidBodyHandle(index); });
        break;
    case ViewportSelectionMode::joint:
        append(SelectionKind::joint, model.joints.size(),
               [&](std::size_t index) { return session.document.jointHandle(index); });
        break;
    }
    return result;
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

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame, bool *open) {
    session.ui.viewportVisible = false;
    if (!ImGui::Begin("ビューポート", open, ImGuiWindowFlags_NoBackground)) {
        ImGui::End();
        return;
    }
    if (session.automation != nullptr) {
        const auto position = ImGui::GetWindowPos();
        const auto windowSize = ImGui::GetWindowSize();
        session.automation->registerWindow(
            "viewport", "ビューポート", static_cast<int>(position.x),
            static_cast<int>(position.y), static_cast<int>(windowSize.x),
            static_cast<int>(windowSize.y));
    }
    const auto modeButton = [&](const char *label, ViewportSelectionMode mode) {
        if (ImGui::RadioButton(label, session.ui.selectionMode == mode)) {
            session.ui.selectionMode = mode;
            if (mode == ViewportSelectionMode::bone)
                session.ui.showBones = true;
            if (mode == ViewportSelectionMode::rigidBody ||
                mode == ViewportSelectionMode::joint)
                session.ui.showPhysics = true;
        }
        if (session.automation != nullptr) {
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            AutomationItem item;
            item.window = "viewport";
            item.id = "viewport/mode:" + std::to_string(static_cast<int>(mode));
            item.role = "button";
            item.label = label;
            item.selected = session.ui.selectionMode == mode;
            item.x = static_cast<int>(minimum.x);
            item.y = static_cast<int>(minimum.y);
            item.width = static_cast<int>(maximum.x - minimum.x);
            item.height = static_cast<int>(maximum.y - minimum.y);
            item.click = [&session, mode]() {
                session.ui.selectionMode = mode;
                if (mode == ViewportSelectionMode::bone)
                    session.ui.showBones = true;
                if (mode == ViewportSelectionMode::rigidBody ||
                    mode == ViewportSelectionMode::joint)
                    session.ui.showPhysics = true;
            };
            session.automation->registerItem(std::move(item));
        }
        ImGui::SameLine();
    };
    modeButton("頂点", ViewportSelectionMode::vertex);
    modeButton("面", ViewportSelectionMode::face);
    modeButton("材質", ViewportSelectionMode::material);
    modeButton("ボーン", ViewportSelectionMode::bone);
    modeButton("剛体", ViewportSelectionMode::rigidBody);
    if (ImGui::RadioButton("ジョイント", session.ui.selectionMode == ViewportSelectionMode::joint)) {
        session.ui.selectionMode = ViewportSelectionMode::joint;
        session.ui.showPhysics = true;
    }
    if (session.automation != nullptr) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        AutomationItem item;
        item.window = "viewport";
        item.id = "viewport/mode:5";
        item.role = "button";
        item.label = "ジョイント";
        item.selected = session.ui.selectionMode == ViewportSelectionMode::joint;
        item.x = static_cast<int>(minimum.x);
        item.y = static_cast<int>(minimum.y);
        item.width = static_cast<int>(maximum.x - minimum.x);
        item.height = static_cast<int>(maximum.y - minimum.y);
        item.click = [&session]() {
            session.ui.selectionMode = ViewportSelectionMode::joint;
            session.ui.showPhysics = true;
        };
        session.automation->registerItem(std::move(item));
    }
    ImGui::SameLine();
    ImGui::Checkbox("X-Ray", &session.ui.xray);
    const auto toolButton = [&](const char *label, ViewportTool tool) {
        if (ImGui::RadioButton(label, session.ui.viewportTool == tool))
            session.ui.viewportTool = tool;
        ImGui::SameLine();
    };
    toolButton("選択", ViewportTool::select);
    toolButton("移動", ViewportTool::move);
    toolButton("回転", ViewportTool::rotate);
    if (ImGui::RadioButton("拡縮", session.ui.viewportTool == ViewportTool::scale))
        session.ui.viewportTool = ViewportTool::scale;
    ImGui::SameLine();
    ImGui::Checkbox("ローカル", &session.ui.localTransform);
    ImGui::SameLine();
    ImGui::Checkbox("スナップ", &session.ui.snapTransform);
    ImGui::SameLine();
    if (ImGui::Button(session.ui.orthographic ? "平行" : "透視"))
        session.ui.orthographic = !session.ui.orthographic;
    ImGui::SameLine();
    ImGui::Checkbox("ボーン表示", &session.ui.showBones);
    ImGui::SameLine();
    ImGui::Checkbox("物理表示", &session.ui.showPhysics);
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
        session.ui.cameraTarget = {(bounds.minX + bounds.maxX) * 0.5F, (bounds.minY + bounds.maxY) * 0.5F,
                                   (bounds.minZ + bounds.maxZ) * 0.5F};
        session.ui.cameraDistance = std::max({bounds.maxX - bounds.minX, bounds.maxY - bounds.minY,
                                              bounds.maxZ - bounds.minZ, 0.1F}) * 2.0F;
        session.ui.cameraInitialized = true;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && ImGui::GetIO().KeyCtrl) {
        const auto delta = ImGui::GetIO().MouseDelta;
        session.ui.cameraDistance = std::clamp(session.ui.cameraDistance * std::exp(delta.y * 0.01F), 0.01F,
                                               100000.0F);
    } else if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && ImGui::GetIO().KeyShift) {
        const auto delta = ImGui::GetIO().MouseDelta;
        const auto scale = session.ui.cameraDistance * 0.0015F;
        session.ui.cameraTarget[0] -= delta.x * std::cos(session.ui.cameraYaw) * scale;
        session.ui.cameraTarget[2] += delta.x * std::sin(session.ui.cameraYaw) * scale;
        session.ui.cameraTarget[1] += delta.y * scale;
    } else if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        session.ui.cameraYaw += delta.x * 0.01F;
        session.ui.cameraPitch = std::clamp(session.ui.cameraPitch + delta.y * 0.01F, -1.5F, 1.5F);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0F)
        session.ui.cameraDistance = std::clamp(session.ui.cameraDistance *
                                                   std::exp(-ImGui::GetIO().MouseWheel * 0.1F),
                                               0.01F, 100000.0F);
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F)) {
        if (const auto position = selectedPosition(session))
            session.ui.cameraTarget = *position;
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Home))
        session.ui.cameraInitialized = false;
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
    if (hovered && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1))
            session.ui.selectionMode = ViewportSelectionMode::vertex;
        if (ImGui::IsKeyPressed(ImGuiKey_2))
            session.ui.selectionMode = ViewportSelectionMode::face;
        if (ImGui::IsKeyPressed(ImGuiKey_3))
            session.ui.selectionMode = ViewportSelectionMode::material;
        if (ImGui::IsKeyPressed(ImGuiKey_4)) {
            session.ui.selectionMode = ViewportSelectionMode::bone;
            session.ui.showBones = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_5)) {
            session.ui.selectionMode = ViewportSelectionMode::rigidBody;
            session.ui.showPhysics = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_6)) {
            session.ui.selectionMode = ViewportSelectionMode::joint;
            session.ui.showPhysics = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            session.ui.viewportTool = ViewportTool::move;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            session.ui.viewportTool = ViewportTool::rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            session.ui.viewportTool = ViewportTool::scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            session.ui.viewportTool = ViewportTool::select;
        if (ImGui::IsKeyPressed(ImGuiKey_A)) {
            if (ImGui::GetIO().KeyAlt)
                session.selection.clear();
            else
                session.selection.set(allItemsForMode(session));
            session.ui.clearDrafts();
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
                                           : IM_COL32(245, 190, 80, 220));
            if (bone.parent < 0 ||
                static_cast<std::size_t>(bone.parent) >= model.bones.size())
                continue;
            draw->AddLine(
                point,
                project(model.bones[static_cast<std::size_t>(bone.parent)].position,
                        bounds, origin, available, session.ui),
                selected ? IM_COL32(255, 225, 90, 255)
                         : IM_COL32(245, 190, 80, 220),
                selected ? 3.0F : 2.0F);
        }
    }
    if (session.ui.showPhysics) {
        for (std::size_t index = 0; index < model.rigidBodies.size(); ++index) {
            const auto &body = model.rigidBodies[index];
            const auto point = project(body.position, bounds, origin, available,
                                       session.ui);
            const auto radius = std::max(3.0F, (std::abs(body.size[0]) + std::abs(body.size[1])) * 0.5F *
                                                   std::min(available.x, available.y) /
                                                   std::max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY));
            const auto selected = isSelected(
                session, SelectionKind::rigidBody,
                session.document.rigidBodyHandle(index));
            const auto color = selected ? IM_COL32(255, 225, 90, 255)
                                        : IM_COL32(180, 230, 255, 180);
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
            const auto selected = isSelected(
                session, SelectionKind::joint,
                session.document.jointHandle(index));
            const auto color = selected ? IM_COL32(255, 225, 90, 255)
                                        : IM_COL32(180, 255, 180, 170);
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
    const CameraState camera{session.ui.cameraTarget, session.ui.cameraYaw, session.ui.cameraPitch,
                             session.ui.cameraDistance, session.ui.orthographic};
    if (drawViewAxis(session.ui, draw, origin, available))
        session.ui.boxSelecting = false;
    if (hovered && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        const auto picked = pickViewport(session, vertices, camera, origin,
                                         available, mouse);
        if (picked) {
            if (!session.selection.contains(picked->item))
                selectViewportItem(session, picked->item, picked->index);
            session.ui.viewportHover = picked->item;
            session.ui.viewportHoverFace = picked->face;
            session.ui.viewportHoverPosition = picked->position;
            ImGui::OpenPopup("viewport-context");
        }
    }
    if (session.ui.boxSelecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImVec2 start{session.ui.boxSelectStartX, session.ui.boxSelectStartY};
        const ImVec2 end{session.ui.boxSelectEndX, session.ui.boxSelectEndY};
        const auto dx = end.x - start.x;
        const auto dy = end.y - start.y;
        if (dx * dx + dy * dy > 16.0F) {
            const auto picked = pickViewportRectangle(session, vertices, camera, origin,
                                                      available, start, end);
            std::vector<SelectionItem> selected;
            selected.reserve(picked.size());
            for (const auto &item : picked)
                selected.push_back(item.item);
            if (ImGui::GetIO().KeyCtrl)
                session.selection.toggle(selected);
            else if (ImGui::GetIO().KeyShift)
                session.selection.add(selected);
            else
                session.selection.set(std::move(selected));
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
        } else {
            const auto picked = pickViewport(session, vertices, camera, origin,
                                             available, end);
            if (picked)
                selectViewportItem(session, picked->item, picked->index);
            else if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
                session.selection.clear();
        }
        session.ui.boxSelecting = false;
    }
    if (!hovered || session.ui.gizmoDragging) {
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
            if (const auto position = selectedPosition(session))
                session.ui.cameraTarget = *position;
            else
                session.ui.cameraTarget = session.ui.viewportHoverPosition;
        }
        const auto materials = selectedMaterials(session);
        if (!materials.empty()) {
            ImGui::Separator();
            if (ImGui::MenuItem("選択材質を隠す")) {
                for (const auto &item : materials)
                    appendUnique(session.ui.hiddenMaterials, item);
                session.ui.status = "選択材質を非表示にしました";
            }
            if (ImGui::MenuItem("選択材質だけ表示")) {
                session.ui.hiddenMaterials.clear();
                session.ui.isolatedMaterials = materials;
                session.ui.status = "選択材質を分離表示しました";
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
                session.selection.set({SelectionKind::texture, texture.domain,
                                       texture.id, texture.generation});
                session.ui.textureIndex = index;
                session.ui.clearDrafts();
            }
        }
        if ((!session.ui.hiddenMaterials.empty() ||
             !session.ui.isolatedMaterials.empty()) &&
            ImGui::MenuItem("すべて表示")) {
            session.ui.hiddenMaterials.clear();
            session.ui.isolatedMaterials.clear();
            session.ui.status = "すべての材質を表示しました";
        }
        ImGui::EndPopup();
    }
    drawViewportGizmo(session, makeCameraMatrices(camera, available.x / available.y), origin, available);
    ImGui::End();
}

} // namespace pmxer
