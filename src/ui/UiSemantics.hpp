#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace pmxer::ui {

enum class UiSemanticId {
    menuFile,
    workspaceModel,
    workspaceRig,
    workspaceMorph,
    workspacePhysics,
    workspaceInspect,
    viewportXray,
    viewportToolMove,
    viewportToolRotate,
    viewportToolScale,
    viewportShowBones,
    viewportShowPhysics,
};

enum class UiSemanticRole { button, checkbox, radio };

struct UiSemanticNode {
    UiSemanticId id{};
    UiSemanticRole role{};
    std::string_view label;
    bool enabled{};
    bool checked{};
    float minX{};
    float minY{};
    float maxX{};
    float maxY{};
};

void beginFrame();
[[nodiscard]] const std::vector<UiSemanticNode> &nodes() noexcept;
[[nodiscard]] std::string_view name(UiSemanticId id) noexcept;

bool button(UiSemanticId id, const char *label, bool enabled = true);
bool checkbox(UiSemanticId id, const char *label, bool *value,
              bool enabled = true);
bool radioButton(UiSemanticId id, const char *label, bool active,
                 bool enabled = true);

} // namespace pmxer::ui
