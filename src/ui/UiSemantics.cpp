#include "UiSemantics.hpp"

#include <imgui.h>

namespace pmxer::ui {
namespace {

std::vector<UiSemanticNode> frameNodes;

void registerItem(UiSemanticId id, UiSemanticRole role, const char *label,
                  bool enabled, bool checked, UiSemanticSupport support,
                  std::string_view reason) {
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    frameNodes.push_back({id, role, label, enabled, checked, support, reason,
                          minimum.x, minimum.y, maximum.x, maximum.y});
}

} // namespace

void beginFrame() { frameNodes.clear(); }

const std::vector<UiSemanticNode> &nodes() noexcept { return frameNodes; }

std::string_view name(UiSemanticId id) noexcept {
    switch (id) {
    case UiSemanticId::menuFile:
        return "menu.file";
    case UiSemanticId::workspaceModel:
        return "workspace.model";
    case UiSemanticId::workspaceRig:
        return "workspace.rig";
    case UiSemanticId::workspaceMorph:
        return "workspace.morph";
    case UiSemanticId::workspacePhysics:
        return "workspace.physics";
    case UiSemanticId::workspaceInspect:
        return "workspace.inspect";
    case UiSemanticId::viewportXray:
        return "viewport.xray";
    case UiSemanticId::viewportToolMove:
        return "viewport.tool.move";
    case UiSemanticId::viewportToolRotate:
        return "viewport.tool.rotate";
    case UiSemanticId::viewportToolScale:
        return "viewport.tool.scale";
    case UiSemanticId::viewportShowBones:
        return "viewport.overlay.bones";
    case UiSemanticId::viewportShowPhysics:
        return "viewport.overlay.physics";
    }
    return "ui.unknown";
}

bool button(UiSemanticId id, const char *label, bool enabled) {
    ImGui::PushID(name(id).data());
    if (!enabled)
        ImGui::BeginDisabled();
    const auto clicked = ImGui::Button(label);
    registerItem(id, UiSemanticRole::button, label, enabled, false,
                 enabled ? UiSemanticSupport::supported
                         : UiSemanticSupport::unsupported,
                 enabled ? std::string_view{} : std::string_view{"操作できません"});
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    return clicked;
}

bool checkbox(UiSemanticId id, const char *label, bool *value, bool enabled) {
    ImGui::PushID(name(id).data());
    if (!enabled)
        ImGui::BeginDisabled();
    const auto changed = ImGui::Checkbox(label, value);
    registerItem(id, UiSemanticRole::checkbox, label, enabled,
                 value != nullptr && *value,
                 enabled ? UiSemanticSupport::supported
                         : UiSemanticSupport::unsupported,
                 enabled ? std::string_view{} : std::string_view{"操作できません"});
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

bool radioButton(UiSemanticId id, const char *label, bool active, bool enabled,
                 UiSemanticSupport support, std::string_view reason) {
    ImGui::PushID(name(id).data());
    if (!enabled)
        ImGui::BeginDisabled();
    const auto clicked = ImGui::RadioButton(label, active);
    registerItem(id, UiSemanticRole::radio, label, enabled, active, support,
                 reason);
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    return clicked;
}

} // namespace pmxer::ui
