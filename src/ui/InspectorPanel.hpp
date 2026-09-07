#pragma once

namespace pmxer {

class DocumentSession;

void drawInspectorPanel(DocumentSession &session, bool *open);
void drawStatusBar(DocumentSession &session, bool &showDiagnostics,
                   bool &showReferences, bool &showDiff);

} // namespace pmxer
