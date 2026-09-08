#pragma once

#include "Selection.hpp"

#include <optional>

namespace pmxer {

class DocumentSession;

enum class EditorWorkspace { model, rig, morph, physics, inspect };

struct WorkspacePolicy {
    EditorWorkspace workspace{};
    std::optional<ViewportSelectionMode> defaultViewportMode;
    bool showBones{};
    bool showPhysics{};

    [[nodiscard]] bool allows(SelectionKind kind) const noexcept;
    [[nodiscard]] bool allows(ViewportSelectionMode mode) const noexcept;
};

[[nodiscard]] WorkspacePolicy workspacePolicy(EditorWorkspace workspace) noexcept;
void applyWorkspacePolicy(DocumentSession &session, const WorkspacePolicy &policy);

} // namespace pmxer
