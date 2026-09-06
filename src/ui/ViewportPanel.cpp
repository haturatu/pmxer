#include "ViewportPanel.hpp"

#include "../render/Camera.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
    const CameraState camera{ui.cameraTarget, ui.cameraYaw, ui.cameraPitch, ui.cameraDistance};
    const auto point = projectWorldToScreen(camera, position, origin.x, origin.y, size.x, size.y);
    return {point.x, point.y};
}

ImVec2 project(const mmd::PmxVertex &vertex, const Bounds &bounds, ImVec2 origin, ImVec2 size,
               const EditorUiState &ui) {
    return project(vertex.position, bounds, origin, size, ui);
}

} // namespace

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame) {
    session.ui.viewportVisible = false;
    ImGui::Begin("ビューポート", nullptr, ImGuiWindowFlags_NoBackground);
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
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        session.ui.cameraYaw += delta.x * 0.01F;
        session.ui.cameraPitch = std::clamp(session.ui.cameraPitch + delta.y * 0.01F, -1.5F, 1.5F);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0F)
        session.ui.cameraDistance = std::clamp(session.ui.cameraDistance *
                                                   std::exp(-ImGui::GetIO().MouseWheel * 0.1F),
                                               0.01F, 100000.0F);

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
            if (handle.id != selected.id || handle.generation != selected.generation ||
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
            if (handle.id != selected.id || handle.generation != selected.generation)
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
        std::size_t closest{};
        float distanceSquared = std::numeric_limits<float>::max();
        const CameraState camera{session.ui.cameraTarget, session.ui.cameraYaw, session.ui.cameraPitch,
                                 session.ui.cameraDistance};
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto point = projectWorldToScreen(camera, vertices[i].position, origin.x, origin.y, available.x,
                                                    available.y);
            if (!point.inFront)
                continue;
            const auto dx = point.x - mouse.x;
            const auto dy = point.y - mouse.y;
            const auto candidate = dx * dx + dy * dy;
            if (candidate < distanceSquared) {
                distanceSquared = candidate;
                closest = i;
            }
        }
        if (distanceSquared <= 18.0F * 18.0F) {
            const auto item = session.document.vertexHandle(closest);
            const SelectionItem value{SelectionKind::vertex, item.id, item.generation};
            if (ImGui::GetIO().KeyCtrl)
                session.selection.add(value);
            else
                session.selection.set(value);
        }
    }
    ImGui::End();
}

} // namespace pmxer
