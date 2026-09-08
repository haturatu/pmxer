#include "EditorSelectionController.hpp"

#include "DocumentSession.hpp"

namespace pmxer {
namespace {

void ensureWorkspaceFor(DocumentSession &session, EditorWorkspace &workspace,
                        SelectionKind kind) {
    if (workspace == EditorWorkspace::inspect ||
        workspacePolicy(workspace).allows(kind))
        return;
    workspace = preferredWorkspace(kind);
    applyWorkspacePolicy(session, workspacePolicy(workspace));
}

} // namespace

EditorWorkspace preferredWorkspace(SelectionKind kind) noexcept {
    switch (kind) {
    case SelectionKind::material:
    case SelectionKind::texture:
    case SelectionKind::face:
        return EditorWorkspace::model;
    case SelectionKind::vertex:
    case SelectionKind::bone:
        return EditorWorkspace::rig;
    case SelectionKind::morph:
        return EditorWorkspace::morph;
    case SelectionKind::rigidBody:
    case SelectionKind::joint:
    case SelectionKind::softBody:
        return EditorWorkspace::physics;
    case SelectionKind::displayFrame:
        return EditorWorkspace::inspect;
    }
    return EditorWorkspace::inspect;
}

void selectPrimary(DocumentSession &session, EditorWorkspace &workspace,
                   SelectionItem item, SelectionOrigin /*origin*/) {
    ensureWorkspaceFor(session, workspace, item.kind);
    session.selection.set(item);
    session.ui.clearDrafts();
}

void addSelection(DocumentSession &session, EditorWorkspace &workspace,
                  SelectionItem item, SelectionOrigin origin) {
    ensureWorkspaceFor(session, workspace, item.kind);
    session.selection.add(item);
    if (origin != SelectionOrigin::morphOffsetTarget)
        session.ui.clearDrafts();
}

void toggleSelection(DocumentSession &session, EditorWorkspace &workspace,
                     SelectionItem item, SelectionOrigin origin) {
    ensureWorkspaceFor(session, workspace, item.kind);
    if (session.selection.contains(item))
        session.selection.remove(item);
    else
        session.selection.add(item);
    if (origin != SelectionOrigin::morphOffsetTarget)
        session.ui.clearDrafts();
}

bool selectMorphOffsetTarget(DocumentSession &session, SelectionItem item) {
    auto &target = session.ui.morphOffsetTarget;
    if (!target.picking)
        return false;
    if (item.kind != target.expectedKind) {
        session.ui.status = "このオフセットが参照する種類を選択してください";
        return true;
    }
    target.target = item;
    target.picking = false;
    session.ui.status = "モーフオフセットの対象を設定しました";
    return true;
}

} // namespace pmxer
