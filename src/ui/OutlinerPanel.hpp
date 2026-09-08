#pragma once

namespace pmxer {

struct DocumentSession;
struct WorkspaceUiState;

void drawOutlinerPanel(DocumentSession &session, WorkspaceUiState &workspace,
                       bool *open);

} // namespace pmxer
