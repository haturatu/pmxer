#pragma once

#include "../editor/WorkspacePolicy.hpp"

namespace pmxer {

struct DocumentSession;
class GpuModelRenderer;

void drawInspectorPanel(DocumentSession &session, GpuModelRenderer *renderer,
                        EditorWorkspace &workspace, bool *open);
void drawStatusBar(DocumentSession &session, bool &showDiagnostics,
                   bool &showReferences, bool &showDiff);

} // namespace pmxer
