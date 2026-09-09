#include "EditorSelectionController.hpp"

#include "DocumentSession.hpp"
#include "UiStatus.hpp"

#include <limits>
#include <optional>
#include <utility>

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

bool outlinerSupports(SelectionKind kind) noexcept {
    switch (kind) {
    case SelectionKind::vertex:
    case SelectionKind::texture:
    case SelectionKind::material:
    case SelectionKind::bone:
    case SelectionKind::morph:
    case SelectionKind::rigidBody:
    case SelectionKind::joint:
    case SelectionKind::softBody:
        return true;
    case SelectionKind::face:
    case SelectionKind::displayFrame:
        return false;
    }
    return false;
}

std::optional<std::size_t> selectionIndex(const DocumentSession &session,
                                          SelectionItem item) {
    const auto &model = session.document.model();
    switch (item.kind) {
    case SelectionKind::vertex:
        for (std::size_t index = 0; index < model.vertices.size(); ++index)
            if (session.document.vertexHandle(index) ==
                selectionHandle<mmd::VertexTag>(session.document, item))
                return index;
        break;
    case SelectionKind::material:
        for (std::size_t index = 0; index < model.materials.size(); ++index)
            if (session.document.materialHandle(index) ==
                selectionHandle<mmd::MaterialTag>(session.document, item))
                return index;
        break;
    case SelectionKind::bone:
        for (std::size_t index = 0; index < model.bones.size(); ++index)
            if (session.document.boneHandle(index) ==
                selectionHandle<mmd::BoneTag>(session.document, item))
                return index;
        break;
    case SelectionKind::morph:
        for (std::size_t index = 0; index < model.morphs.size(); ++index)
            if (session.document.morphHandle(index) ==
                selectionHandle<mmd::MorphTag>(session.document, item))
                return index;
        break;
    case SelectionKind::rigidBody:
        for (std::size_t index = 0; index < model.rigidBodies.size(); ++index)
            if (session.document.rigidBodyHandle(index) ==
                selectionHandle<mmd::RigidBodyTag>(session.document, item))
                return index;
        break;
    default:
        break;
    }
    return std::nullopt;
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
                   SelectionItem item, SelectionOrigin origin) {
    ensureWorkspaceFor(session, workspace, item.kind);
    session.selection.set(item);
    session.ui.clearDrafts();
    session.ui.morphOffsetTarget = {};
    session.ui.morphAddTarget.reset();
    if (origin == SelectionOrigin::outliner || !outlinerSupports(item.kind))
        session.ui.pendingOutlinerReveal.reset();
    else
        session.ui.pendingOutlinerReveal = item;
}

void selectMany(DocumentSession &session, EditorWorkspace &workspace,
                std::vector<SelectionItem> items, SelectionOrigin origin) {
    if (items.empty())
        return;
    ensureWorkspaceFor(session, workspace, items.front().kind);
    session.selection.set(std::move(items));
    session.ui.clearDrafts();
    session.ui.morphOffsetTarget = {};
    session.ui.morphAddTarget.reset();
    if (origin == SelectionOrigin::outliner ||
        !outlinerSupports(session.selection.items().front().kind))
        session.ui.pendingOutlinerReveal.reset();
    else
        session.ui.pendingOutlinerReveal = session.selection.items().front();
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

bool selectAllForMode(DocumentSession &session, EditorWorkspace &workspace,
                      ViewportSelectionMode mode,
                      SelectionOrigin /*origin*/) {
    std::vector<SelectionItem> items;
    const auto &model = session.document.model();
    const auto append = [&](SelectionKind kind, std::size_t count,
                            const auto &handleAt) {
        items.reserve(items.size() + count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto handle = handleAt(index);
            items.push_back(
                {kind, handle.domain, handle.id, handle.generation});
        }
    };
    if (workspace == EditorWorkspace::morph) {
        append(SelectionKind::morph, model.morphs.size(),
               [&](std::size_t index) { return session.document.morphHandle(index); });
    } else {
        if (!workspacePolicy(workspace).allows(mode))
            return false;
        switch (mode) {
        case ViewportSelectionMode::vertex:
            append(SelectionKind::vertex, model.vertices.size(),
                   [&](std::size_t index) { return session.document.vertexHandle(index); });
            break;
        case ViewportSelectionMode::face:
            append(SelectionKind::face, model.indices.size() / 3U,
                   [&](std::size_t index) { return session.document.faceHandle(index); });
            break;
        case ViewportSelectionMode::material:
            append(SelectionKind::material, model.materials.size(),
                   [&](std::size_t index) { return session.document.materialHandle(index); });
            break;
        case ViewportSelectionMode::bone:
            append(SelectionKind::bone, model.bones.size(),
                   [&](std::size_t index) { return session.document.boneHandle(index); });
            break;
        case ViewportSelectionMode::rigidBody:
            append(SelectionKind::rigidBody, model.rigidBodies.size(),
                   [&](std::size_t index) { return session.document.rigidBodyHandle(index); });
            break;
        case ViewportSelectionMode::joint:
            append(SelectionKind::joint, model.joints.size(),
                   [&](std::size_t index) { return session.document.jointHandle(index); });
            break;
        }
    }
    session.selection.set(std::move(items));
    session.ui.clearDrafts();
    return true;
}

bool selectMorphOffsetTarget(DocumentSession &session, SelectionItem item) {
    auto &target = session.ui.morphOffsetTarget;
    if (!target.picking)
        return false;
    if (item.kind != target.expectedKind) {
        setStatus(session, "このオフセットが参照する種類を選択してください",
                  UiStatusKind::warning);
        return true;
    }
    const auto index = selectionIndex(session, item);
    if (!index || *index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        setStatus(session, "オフセット対象のインデックスを解決できません",
                  UiStatusKind::error, std::chrono::milliseconds::zero(), true);
        return true;
    }
    if (target.adding) {
        session.ui.morphAddTarget = item;
    } else {
        const auto *morph = session.document.resolve(target.morph);
        if (morph == nullptr || target.offsetIndex >= morph->offsets.size()) {
            setStatus(session, "モーフオフセットが見つかりません",
                      UiStatusKind::error, std::chrono::milliseconds::zero(), true);
            return true;
        }
        if (!session.ui.morphDraft)
            session.ui.morphDraft = *morph;
        if (target.offsetIndex >= session.ui.morphDraft->offsets.size()) {
            setStatus(session, "モーフオフセットの編集状態が古くなっています",
                      UiStatusKind::error, std::chrono::milliseconds::zero(), true);
            return true;
        }
        session.ui.morphDraft->offsets[target.offsetIndex].index =
            static_cast<std::int32_t>(*index);
        session.ui.morphOffsetDirty = true;
        ++session.ui.morphOffsetFilterEpoch;
    }
    target.target = item;
    target.picking = false;
    setStatus(session, "モーフオフセットの対象を設定しました",
              UiStatusKind::success);
    return true;
}

} // namespace pmxer
