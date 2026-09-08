#include "WorkspacePolicy.hpp"

#include "DocumentSession.hpp"

#include <limits>
#include <utility>
#include <vector>

namespace pmxer {

bool WorkspacePolicy::allows(SelectionKind kind) const noexcept {
    switch (workspace) {
    case EditorWorkspace::model:
        return kind == SelectionKind::material || kind == SelectionKind::texture ||
               kind == SelectionKind::face;
    case EditorWorkspace::rig:
        return kind == SelectionKind::vertex || kind == SelectionKind::bone;
    case EditorWorkspace::morph:
        return kind == SelectionKind::morph;
    case EditorWorkspace::physics:
        return kind == SelectionKind::rigidBody || kind == SelectionKind::joint ||
               kind == SelectionKind::softBody;
    case EditorWorkspace::inspect:
        return true;
    }
    return false;
}

bool WorkspacePolicy::allows(ViewportSelectionMode mode) const noexcept {
    switch (mode) {
    case ViewportSelectionMode::vertex:
        return allows(SelectionKind::vertex);
    case ViewportSelectionMode::face:
        return allows(SelectionKind::face);
    case ViewportSelectionMode::material:
        return allows(SelectionKind::material);
    case ViewportSelectionMode::bone:
        return allows(SelectionKind::bone);
    case ViewportSelectionMode::rigidBody:
        return allows(SelectionKind::rigidBody);
    case ViewportSelectionMode::joint:
        return allows(SelectionKind::joint);
    }
    return false;
}

WorkspacePolicy workspacePolicy(EditorWorkspace workspace) noexcept {
    const auto profile = defaultViewportProfile(workspace);
    switch (workspace) {
    case EditorWorkspace::model:
        return {workspace, ViewportSelectionMode::material, false, false, profile};
    case EditorWorkspace::rig:
        return {workspace, ViewportSelectionMode::bone, true, false, profile};
    case EditorWorkspace::morph:
        return {workspace, std::nullopt, false, false, profile};
    case EditorWorkspace::physics:
        return {workspace, ViewportSelectionMode::rigidBody, false, true, profile};
    case EditorWorkspace::inspect:
        return {workspace, std::nullopt, false, false, profile};
    }
    return {EditorWorkspace::model, ViewportSelectionMode::material, false, false,
            defaultViewportProfile(EditorWorkspace::model)};
}

WorkspaceViewportProfile defaultViewportProfile(EditorWorkspace workspace) noexcept {
    switch (workspace) {
    case EditorWorkspace::rig:
        return {true, false, 0.8F, 0.75F};
    case EditorWorkspace::physics:
        return {false, true, 0.8F, 0.75F};
    case EditorWorkspace::model:
    case EditorWorkspace::morph:
    case EditorWorkspace::inspect:
        return {false, false, 0.8F, 0.75F};
    }
    return {};
}

std::size_t workspaceIndex(EditorWorkspace workspace) noexcept {
    return static_cast<std::size_t>(workspace);
}

void applyViewportProfile(DocumentSession &session,
                          const WorkspaceViewportProfile &profile) {
    session.ui.showBones = profile.showBones;
    session.ui.showPhysics = profile.showPhysics;
}

void applyWorkspacePolicy(DocumentSession &session, const WorkspacePolicy &policy) {
    std::vector<SelectionItem> retained;
    retained.reserve(session.selection.items().size());
    for (const auto &item : session.selection.items())
        if (policy.allows(item.kind))
            retained.push_back(item);
    session.selection.set(std::move(retained));
    session.ui.clearDrafts();
    session.ui.morphOffsetTarget = {};
    session.ui.viewportHover.reset();
    session.ui.viewportHoverFace.reset();
    session.ui.viewportHoverRevision = std::numeric_limits<std::uint64_t>::max();
    session.ui.viewportHoverFrameRevision = std::numeric_limits<std::uint64_t>::max();
    session.ui.viewportTool = ViewportTool::select;
    applyViewportProfile(session, policy.viewportProfile);
    if (!policy.allows(session.ui.selectionMode) && policy.defaultViewportMode)
        session.ui.selectionMode = *policy.defaultViewportMode;
}

} // namespace pmxer
