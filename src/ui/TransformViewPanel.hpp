#pragma once

namespace pmxer {

struct DocumentSession;
struct WorkspaceUiState;

void drawTransformView(DocumentSession &session, WorkspaceUiState &workspace,
                       bool *open);

} // namespace pmxer
