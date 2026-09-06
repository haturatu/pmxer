#include "ViewportPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pmxer {
namespace {

struct Bounds {
    float minX{std::numeric_limits<float>::max()};
    float maxX{std::numeric_limits<float>::lowest()};
    float minY{std::numeric_limits<float>::max()};
    float maxY{std::numeric_limits<float>::lowest()};
};

ImVec2 project(const mmd::PmxVertex &vertex, const Bounds &bounds, ImVec2 origin, ImVec2 size) {
    const auto width = std::max(bounds.maxX - bounds.minX, 0.001F);
    const auto height = std::max(bounds.maxY - bounds.minY, 0.001F);
    const auto scale = std::min(size.x / width, size.y / height) * 0.8F;
    return {origin.x + size.x * 0.5F + (vertex.position[0] - (bounds.minX + bounds.maxX) * 0.5F) * scale,
            origin.y + size.y * 0.5F - (vertex.position[1] - (bounds.minY + bounds.maxY) * 0.5F) * scale};
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
    }
    if (vertices.empty()) {
        draw->AddText({origin.x + 16.0F, origin.y + 16.0F}, IM_COL32_WHITE, "頂点がありません");
        ImGui::End();
        return;
    }

    for (std::size_t i = 0; i + 2 < model.indices.size(); i += 3) {
        const auto a = model.indices[i];
        const auto b = model.indices[i + 1];
        const auto c = model.indices[i + 2];
        if (a >= model.vertices.size() || b >= model.vertices.size() || c >= model.vertices.size())
            continue;
        const auto pa = project(vertices[a], bounds, origin, available);
        const auto pb = project(vertices[b], bounds, origin, available);
        const auto pc = project(vertices[c], bounds, origin, available);
        draw->AddLine(pa, pb, IM_COL32(115, 145, 190, 180));
        draw->AddLine(pb, pc, IM_COL32(115, 145, 190, 180));
        draw->AddLine(pc, pa, IM_COL32(115, 145, 190, 180));
    }
    for (std::size_t i = 0; i < vertices.size() && i < 10000; ++i) {
        const auto point = project(vertices[i], bounds, origin, available);
        const auto handle = session.document.vertexHandle(i);
        const bool selected = session.selection.contains({SelectionKind::vertex, handle.id, handle.generation});
        draw->AddCircleFilled(point, selected ? 4.0F : 2.0F, selected ? IM_COL32(255, 220, 80, 255)
                                                                        : IM_COL32(210, 215, 225, 180));
    }
    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        const auto &bone = model.bones[i];
        if (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= model.bones.size())
            continue;
        mmd::PmxVertex from;
        mmd::PmxVertex to;
        from.position = bone.position;
        to.position = model.bones[static_cast<std::size_t>(bone.parent)].position;
        draw->AddLine(project(from, bounds, origin, available), project(to, bounds, origin, available),
                      IM_COL32(245, 190, 80, 220), 2.0F);
    }
    if (clicked && !vertices.empty()) {
        const auto mouse = ImGui::GetIO().MousePos;
        std::size_t closest{};
        float distanceSquared = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto point = project(vertices[i], bounds, origin, available);
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
