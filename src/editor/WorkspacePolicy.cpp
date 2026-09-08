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
    if (workspace == EditorWorkspace::morph && mode == ViewportSelectionMode::material)
        return true;
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
    switch (workspace) {
    case EditorWorkspace::model:
        return {workspace, ViewportSelectionMode::material, false, false};
    case EditorWorkspace::rig:
        return {workspace, ViewportSelectionMode::bone, true, false};
    case EditorWorkspace::morph:
        return {workspace, ViewportSelectionMode::material, false, false};
    case EditorWorkspace::physics:
        return {workspace, ViewportSelectionMode::rigidBody, false, true};
    case EditorWorkspace::inspect:
        return {workspace, std::nullopt, true, true};
    }
    return {EditorWorkspace::model, ViewportSelectionMode::material, false, false};
}

void applyWorkspacePolicy(DocumentSession &session, const WorkspacePolicy &policy) {
    std::vector<SelectionItem> retained;
    retained.reserve(session.selection.items().size());
    for (const auto &item : session.selection.items())
        if (policy.allows(item.kind))
            retained.push_back(item);
    session.selection.set(std::move(retained));
    session.ui.clearDrafts();
    session.ui.viewportHover.reset();
    session.ui.viewportHoverFace.reset();
    session.ui.viewportHoverRevision = std::numeric_limits<std::uint64_t>::max();
    session.ui.viewportHoverFrameRevision = std::numeric_limits<std::uint64_t>::max();
    session.ui.viewportTool = ViewportTool::select;
    session.ui.showBones = policy.showBones;
    session.ui.showPhysics = policy.showPhysics;
    if (!policy.allows(session.ui.selectionMode) && policy.defaultViewportMode)
        session.ui.selectionMode = *policy.defaultViewportMode;
}

} // namespace pmxer
