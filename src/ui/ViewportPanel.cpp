#include "ViewportPanel.hpp"

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

ImVec2 project(const mmd::Float3 &position, const Bounds &bounds, ImVec2 origin, ImVec2 size,
               const EditorUiState &ui) {
    const auto radius = std::max({bounds.maxX - bounds.minX, bounds.maxY - bounds.minY, bounds.maxZ - bounds.minZ, 0.001F});
    const auto scale = std::min(size.x / radius, size.y / radius) * 0.8F;
    const auto dx = position[0] - ui.cameraTarget[0];
    const auto dy = position[1] - ui.cameraTarget[1];
    const auto dz = position[2] - ui.cameraTarget[2];
    const auto cosYaw = std::cos(ui.cameraYaw);
    const auto sinYaw = std::sin(ui.cameraYaw);
    const auto yawX = cosYaw * dx + sinYaw * dz;
    const auto yawZ = -sinYaw * dx + cosYaw * dz;
    const auto cosPitch = std::cos(ui.cameraPitch);
    const auto sinPitch = std::sin(ui.cameraPitch);
    const auto viewY = cosPitch * dy - sinPitch * yawZ;
    const auto viewZ = sinPitch * dy + cosPitch * yawZ;
    const auto perspective = ui.cameraDistance / std::max(ui.cameraDistance + viewZ, 0.1F);
    return {origin.x + size.x * 0.5F + yawX * scale * perspective,
            origin.y + size.y * 0.5F - viewY * scale * perspective};
}

ImVec2 project(const mmd::PmxVertex &vertex, const Bounds &bounds, ImVec2 origin, ImVec2 size,
               const EditorUiState &ui) {
    return project(vertex.position, bounds, origin, size, ui);
}

ImU32 weightColor(float weight) {
    const auto value = std::clamp(weight, 0.0F, 1.0F);
    const auto red = static_cast<int>(255.0F * value);
    const auto blue = static_cast<int>(255.0F * (1.0F - value));
    return IM_COL32(red, 80, blue, 210);
}

ImU32 materialColor(const mmd::PmxMaterial &material) {
    const auto color = ImVec4{std::clamp(material.diffuse[0], 0.0F, 1.0F),
                              std::clamp(material.diffuse[1], 0.0F, 1.0F),
                              std::clamp(material.diffuse[2], 0.0F, 1.0F),
                              std::clamp(material.diffuse[3], 0.0F, 1.0F)};
    return ImGui::ColorConvertFloat4ToU32(color);
}

} // namespace

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame) {
    ImGui::Begin("ビューポート");
    const auto available = ImGui::GetContentRegionAvail();
    if (available.x < 10.0F || available.y < 10.0F) {
        ImGui::End();
        return;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("viewport-canvas", available);
    const auto hovered = ImGui::IsItemHovered();
    const auto clicked = hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + available.x, origin.y + available.y}, IM_COL32(16, 19, 25, 255));

    const auto &model = session.document.model();
    const auto &vertices = frame != nullptr && !frame->vertices.empty() ? frame->vertices : model.vertices;
    Bounds bounds;
    for (const auto &vertex : vertices) {
        bounds.minX = std::min(bounds.minX, vertex.position[0]);
        bounds.maxX = std::max(bounds.maxX, vertex.position[0]);
        bounds.minY = std::min(bounds.minY, vertex.position[1]);
        bounds.maxY = std::max(bounds.maxY, vertex.position[1]);
        bounds.minZ = std::min(bounds.minZ, vertex.position[2]);
        bounds.maxZ = std::max(bounds.maxZ, vertex.position[2]);
    }
    if (vertices.empty()) {
        draw->AddText({origin.x + 16.0F, origin.y + 16.0F}, IM_COL32_WHITE, "頂点がありません");
        ImGui::End();
        return;
    }
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

    std::size_t indexBegin = 0;
    for (const auto &material : model.materials) {
        const auto indexEnd = std::min(model.indices.size(), indexBegin + static_cast<std::size_t>(material.indexCount));
        const auto color = materialColor(material);
        for (std::size_t i = indexBegin; i + 2 < indexEnd; i += 3) {
            const auto a = model.indices[i];
            const auto b = model.indices[i + 1];
            const auto c = model.indices[i + 2];
            if (a >= model.vertices.size() || b >= model.vertices.size() || c >= model.vertices.size())
                continue;
            draw->AddTriangleFilled(project(vertices[a], bounds, origin, available, session.ui),
                                    project(vertices[b], bounds, origin, available, session.ui),
                                    project(vertices[c], bounds, origin, available, session.ui), color);
        }
        indexBegin = indexEnd;
    }
    for (std::size_t i = 0; i + 2 < model.indices.size(); i += 3) {
        const auto a = model.indices[i];
        const auto b = model.indices[i + 1];
        const auto c = model.indices[i + 2];
        if (a >= model.vertices.size() || b >= model.vertices.size() || c >= model.vertices.size())
            continue;
        const auto pa = project(vertices[a], bounds, origin, available, session.ui);
        const auto pb = project(vertices[b], bounds, origin, available, session.ui);
        const auto pc = project(vertices[c], bounds, origin, available, session.ui);
        draw->AddLine(pa, pb, IM_COL32(115, 145, 190, 180));
        draw->AddLine(pb, pc, IM_COL32(115, 145, 190, 180));
        draw->AddLine(pc, pa, IM_COL32(115, 145, 190, 180));
    }
    for (std::size_t i = 0; i < vertices.size() && i < 10000; ++i) {
        const auto point = project(vertices[i], bounds, origin, available, session.ui);
        const auto handle = session.document.vertexHandle(i);
        const bool selected = session.selection.contains({SelectionKind::vertex, handle.id, handle.generation});
        const auto vertexColor = selected ? IM_COL32(255, 220, 80, 255) : weightColor(model.vertices[i].weights[0]);
        draw->AddCircleFilled(point, selected ? 4.0F : 2.0F, vertexColor);
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
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto point = project(vertices[i], bounds, origin, available, session.ui);
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
