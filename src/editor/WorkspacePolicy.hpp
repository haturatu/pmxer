#pragma once

#include "Selection.hpp"

#include <cstddef>
#include <optional>

namespace pmxer {

struct DocumentSession;

enum class EditorWorkspace { model, rig, morph, physics, inspect };

enum class PhysicsOverlayMode { context, all, selectedOnly };

struct WorkspaceViewportProfile {
    bool showBones{};
    bool showPhysics{};
    float boneOpacity{0.8F};
    float physicsOpacity{0.25F};
    PhysicsOverlayMode physicsMode{PhysicsOverlayMode::context};
};

struct WorkspacePolicy {
    EditorWorkspace workspace{};
    std::optional<ViewportSelectionMode> defaultViewportMode;
    bool showBones{};
    bool showPhysics{};
    WorkspaceViewportProfile viewportProfile{};

    [[nodiscard]] bool allows(SelectionKind kind) const noexcept;
    [[nodiscard]] bool allows(ViewportSelectionMode mode) const noexcept;
};

[[nodiscard]] WorkspacePolicy workspacePolicy(EditorWorkspace workspace) noexcept;
[[nodiscard]] WorkspaceViewportProfile defaultViewportProfile(EditorWorkspace workspace) noexcept;
[[nodiscard]] std::size_t workspaceIndex(EditorWorkspace workspace) noexcept;
void applyViewportProfile(DocumentSession &session,
                          const WorkspaceViewportProfile &profile);
void applyWorkspacePolicy(DocumentSession &session, const WorkspacePolicy &policy);

} // namespace pmxer
