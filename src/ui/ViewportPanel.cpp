#include "ViewportPanel.hpp"

#include "../render/Camera.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>

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
    if (ImGui::GetIO().KeyCtrl)
        session.selection.add(item);
    else
        session.selection.set(item);
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

bool insideTriangle(ImVec2 point, ImVec2 a, ImVec2 b, ImVec2 c) {
    const auto sign = [](ImVec2 p1, ImVec2 p2, ImVec2 p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    const auto first = sign(point, a, b);
    const auto second = sign(point, b, c);
    const auto third = sign(point, c, a);
    return !((first < 0.0F || second < 0.0F || third < 0.0F) &&
             (first > 0.0F || second > 0.0F || third > 0.0F));
}

std::size_t materialForIndex(const mmd::PmxModel &model, std::size_t indexOffset) {
    std::size_t end{};
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        end += model.materials[index].indexCount;
        if (indexOffset < end)
            return index;
    }
    return model.materials.empty() ? 0U : model.materials.size() - 1U;
}

} // namespace

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame, bool *open) {
    session.ui.viewportVisible = false;
    if (!ImGui::Begin("ビューポート", open, ImGuiWindowFlags_NoBackground)) {
        ImGui::End();
        return;
    }
    const auto modeButton = [&](const char *label, ViewportSelectionMode mode) {
        if (ImGui::RadioButton(label, session.ui.selectionMode == mode))
            session.ui.selectionMode = mode;
        ImGui::SameLine();
    };
    modeButton("頂点", ViewportSelectionMode::vertex);
    modeButton("面", ViewportSelectionMode::face);
    modeButton("材質", ViewportSelectionMode::material);
    modeButton("ボーン", ViewportSelectionMode::bone);
    modeButton("剛体", ViewportSelectionMode::rigidBody);
    if (ImGui::RadioButton("ジョイント", session.ui.selectionMode == ViewportSelectionMode::joint))
        session.ui.selectionMode = ViewportSelectionMode::joint;
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
    const auto clicked = hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left);
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
        if (ImGui::IsKeyPressed(ImGuiKey_4))
            session.ui.selectionMode = ViewportSelectionMode::bone;
        if (ImGui::IsKeyPressed(ImGuiKey_5))
            session.ui.selectionMode = ViewportSelectionMode::rigidBody;
        if (ImGui::IsKeyPressed(ImGuiKey_6))
            session.ui.selectionMode = ViewportSelectionMode::joint;
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            session.ui.viewportTool = ViewportTool::move;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            session.ui.viewportTool = ViewportTool::rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            session.ui.viewportTool = ViewportTool::scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            session.ui.viewportTool = ViewportTool::select;
        if (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyAlt)
            session.ui.xray = !session.ui.xray;
    }

    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        const auto &bone = model.bones[i];
        if (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= model.bones.size())
            continue;
        mmd::PmxVertex from;
        mmd::PmxVertex to;
        from.position = bone.position;
        to.position = model.bones[static_cast<std::size_t>(bone.parent)].position;
        draw->AddLine(project(from, bounds, origin, available, session.ui), project(to, bounds, origin, available, session.ui),
                      IM_COL32(245, 190, 80, 220), 2.0F);
    }
    for (const auto &body : model.rigidBodies) {
        mmd::PmxVertex center;
        center.position = body.position;
        const auto point = project(center, bounds, origin, available, session.ui);
        const auto radius = std::max(3.0F, (std::abs(body.size[0]) + std::abs(body.size[1])) * 0.5F *
                                               std::min(available.x, available.y) /
                                               std::max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY));
        if (body.shape == 1)
            draw->AddRect({point.x - radius, point.y - radius}, {point.x + radius, point.y + radius},
                          IM_COL32(180, 230, 255, 180));
        else
            draw->AddCircle(point, radius, IM_COL32(180, 230, 255, 180));
    }
    for (const auto &joint : model.joints) {
        if (joint.bodyA < 0 || joint.bodyB < 0 || static_cast<std::size_t>(joint.bodyA) >= model.rigidBodies.size() ||
            static_cast<std::size_t>(joint.bodyB) >= model.rigidBodies.size())
            continue;
        mmd::PmxVertex first;
        mmd::PmxVertex second;
        first.position = model.rigidBodies[static_cast<std::size_t>(joint.bodyA)].position;
        second.position = model.rigidBodies[static_cast<std::size_t>(joint.bodyB)].position;
        draw->AddLine(project(first, bounds, origin, available, session.ui), project(second, bounds, origin, available, session.ui),
                      IM_COL32(180, 255, 180, 170), 1.0F);
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
    if (clicked && !vertices.empty()) {
        const auto mouse = ImGui::GetIO().MousePos;
        const CameraState camera{session.ui.cameraTarget, session.ui.cameraYaw, session.ui.cameraPitch,
                                 session.ui.cameraDistance, session.ui.orthographic};

        if (session.ui.selectionMode == ViewportSelectionMode::face ||
            session.ui.selectionMode == ViewportSelectionMode::material) {
            std::optional<std::size_t> hit;
            float hitDepth = std::numeric_limits<float>::max();
            for (std::size_t face = 0; face < model.indices.size() / 3U; ++face) {
                const auto offset = face * 3U;
                const auto firstIndex = static_cast<std::size_t>(model.indices[offset]);
                const auto secondIndex = static_cast<std::size_t>(model.indices[offset + 1U]);
                const auto thirdIndex = static_cast<std::size_t>(model.indices[offset + 2U]);
                if (firstIndex >= vertices.size() || secondIndex >= vertices.size() || thirdIndex >= vertices.size())
                    continue;
                const auto first = projectWorldToScreen(camera, vertices[firstIndex].position, origin.x, origin.y,
                                                        available.x, available.y);
                const auto second = projectWorldToScreen(camera, vertices[secondIndex].position, origin.x, origin.y,
                                                         available.x, available.y);
                const auto third = projectWorldToScreen(camera, vertices[thirdIndex].position, origin.x, origin.y,
                                                        available.x, available.y);
                if (!first.inFront || !second.inFront || !third.inFront)
                    continue;
                const auto depth = (first.depth + second.depth + third.depth) / 3.0F;
                if (depth < hitDepth &&
                    insideTriangle(mouse, {first.x, first.y}, {second.x, second.y}, {third.x, third.y})) {
                    hit = face;
                    hitDepth = depth;
                }
            }
            if (hit && session.ui.selectionMode == ViewportSelectionMode::face) {
                const auto handle = session.document.faceHandle(*hit);
                selectViewportItem(session,
                                   {SelectionKind::face, handle.domain, handle.id, handle.generation}, *hit);
            } else if (hit && !model.materials.empty()) {
                const auto index = materialForIndex(model, *hit * 3U);
                const auto handle = session.document.materialHandle(index);
                selectViewportItem(session,
                                   {SelectionKind::material, handle.domain, handle.id, handle.generation}, index);
            }
        } else {
            std::size_t closest{};
            float distanceSquared = std::numeric_limits<float>::max();
            const auto consider = [&](const mmd::Float3 &position, std::size_t index) {
                const auto point = projectWorldToScreen(camera, position, origin.x, origin.y, available.x,
                                                        available.y);
                if (!point.inFront)
                    return;
                const auto dx = point.x - mouse.x;
                const auto dy = point.y - mouse.y;
                const auto candidate = dx * dx + dy * dy;
                if (candidate < distanceSquared) {
                    distanceSquared = candidate;
                    closest = index;
                }
            };
            if (session.ui.selectionMode == ViewportSelectionMode::vertex) {
                for (std::size_t index = 0; index < vertices.size(); ++index)
                    consider(vertices[index].position, index);
            } else if (session.ui.selectionMode == ViewportSelectionMode::bone) {
                for (std::size_t index = 0; index < model.bones.size(); ++index)
                    consider(model.bones[index].position, index);
            } else if (session.ui.selectionMode == ViewportSelectionMode::rigidBody) {
                for (std::size_t index = 0; index < model.rigidBodies.size(); ++index)
                    consider(model.rigidBodies[index].position, index);
            } else if (session.ui.selectionMode == ViewportSelectionMode::joint) {
                for (std::size_t index = 0; index < model.joints.size(); ++index)
                    consider(model.joints[index].position, index);
            }
            if (distanceSquared <= 18.0F * 18.0F) {
                if (session.ui.selectionMode == ViewportSelectionMode::vertex) {
                    const auto handle = session.document.vertexHandle(closest);
                    selectViewportItem(session,
                                       {SelectionKind::vertex, handle.domain, handle.id, handle.generation}, closest);
                } else if (session.ui.selectionMode == ViewportSelectionMode::bone) {
                    const auto handle = session.document.boneHandle(closest);
                    selectViewportItem(session,
                                       {SelectionKind::bone, handle.domain, handle.id, handle.generation}, closest);
                } else if (session.ui.selectionMode == ViewportSelectionMode::rigidBody) {
                    const auto handle = session.document.rigidBodyHandle(closest);
                    selectViewportItem(session,
                                       {SelectionKind::rigidBody, handle.domain, handle.id, handle.generation}, closest);
                } else if (session.ui.selectionMode == ViewportSelectionMode::joint) {
                    const auto handle = session.document.jointHandle(closest);
                    selectViewportItem(session,
                                       {SelectionKind::joint, handle.domain, handle.id, handle.generation}, closest);
                }
            }
        }
    }
    ImGui::End();
}

} // namespace pmxer
