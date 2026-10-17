#pragma once
#include <memory>
namespace pmxer {
struct DocumentSession;
struct WorkspaceUiState;
struct AiEditState;
void drawAiEditPanel(DocumentSession *, WorkspaceUiState &);
[[nodiscard]] DocumentSession &aiDisplaySession(DocumentSession &, WorkspaceUiState &);
} // namespace pmxer
