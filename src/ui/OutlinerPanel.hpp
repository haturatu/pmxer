#pragma once

namespace pmxer {

class DocumentSession;
struct WorkspaceUiState;

void drawOutlinerPanel(DocumentSession &session, WorkspaceUiState &workspace,
                       bool *open);

} // namespace pmxer
