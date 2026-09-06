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

void drawViewportPanel(DocumentSession &session) {
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

    Bounds bounds;
    for (const auto &vertex : session.document.model().vertices) {
        bounds.minX = std::min(bounds.minX, vertex.position[0]);
        bounds.maxX = std::max(bounds.maxX, vertex.position[0]);
        bounds.minY = std::min(bounds.minY, vertex.position[1]);
        bounds.maxY = std::max(bounds.maxY, vertex.position[1]);
    }
    if (session.document.model().vertices.empty()) {
        draw->AddText({origin.x + 16.0F, origin.y + 16.0F}, IM_COL32_WHITE, "頂点がありません");
        ImGui::End();
        return;
    }

    const auto &model = session.document.model();
    for (std::size_t i = 0; i + 2 < model.indices.size(); i += 3) {
        const auto a = model.indices[i];
        const auto b = model.indices[i + 1];
        const auto c = model.indices[i + 2];
        if (a >= model.vertices.size() || b >= model.vertices.size() || c >= model.vertices.size())
            continue;
        const auto pa = project(model.vertices[a], bounds, origin, available);
        const auto pb = project(model.vertices[b], bounds, origin, available);
        const auto pc = project(model.vertices[c], bounds, origin, available);
        draw->AddLine(pa, pb, IM_COL32(115, 145, 190, 180));
        draw->AddLine(pb, pc, IM_COL32(115, 145, 190, 180));
        draw->AddLine(pc, pa, IM_COL32(115, 145, 190, 180));
    }
    for (std::size_t i = 0; i < model.vertices.size() && i < 10000; ++i) {
        const auto point = project(model.vertices[i], bounds, origin, available);
        draw->AddCircleFilled(point, 2.0F, IM_COL32(210, 215, 225, 180));
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
    if (clicked && !model.vertices.empty()) {
        const auto item = session.document.vertexHandle(0);
        session.selection.set({SelectionKind::vertex, item.id, item.generation});
    }
    ImGui::End();
}

} // namespace pmxer

