#pragma once

#include "../editor/WorkspacePolicy.hpp"

namespace pmxer {

struct DocumentSession;
struct WorkspaceUiState;
class GpuModelRenderer;

void drawInspectorPanel(DocumentSession &session, GpuModelRenderer *renderer,
                        WorkspaceUiState &workspace, bool *open);
void drawStatusBar(DocumentSession &session, bool &showDiagnostics,
                   bool &showReferences, bool &showDiff);

} // namespace pmxer
