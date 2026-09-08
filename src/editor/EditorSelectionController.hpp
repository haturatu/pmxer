#pragma once

#include "Selection.hpp"
#include "WorkspacePolicy.hpp"

namespace pmxer {

class DocumentSession;

enum class SelectionOrigin {
    viewport,
    outliner,
    inspectorLink,
    diagnostics,
    reference,
    morphOffsetTarget,
};

[[nodiscard]] EditorWorkspace preferredWorkspace(SelectionKind kind) noexcept;

void selectPrimary(DocumentSession &session, EditorWorkspace &workspace,
                   SelectionItem item, SelectionOrigin origin);
void addSelection(DocumentSession &session, EditorWorkspace &workspace,
                  SelectionItem item, SelectionOrigin origin);
void toggleSelection(DocumentSession &session, EditorWorkspace &workspace,
                     SelectionItem item, SelectionOrigin origin);
[[nodiscard]] bool selectMorphOffsetTarget(DocumentSession &session,
                                           SelectionItem item);

} // namespace pmxer
