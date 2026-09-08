#pragma once

namespace pmxer {

class DocumentSession;
class GpuModelRenderer;

void drawInspectorPanel(DocumentSession &session, GpuModelRenderer *renderer,
                        bool *open);
void drawStatusBar(DocumentSession &session, bool &showDiagnostics,
                   bool &showReferences, bool &showDiff);

} // namespace pmxer
