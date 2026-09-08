#pragma once

#include "../editor/DocumentSession.hpp"
#include "../editor/WorkspacePolicy.hpp"

#include <mmd/animation.hpp>

namespace pmxer {

class GpuModelRenderer;

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame,
                       GpuModelRenderer *renderer,
                       bool *showDiagnostics, bool *open,
                       EditorWorkspace &activeWorkspace,
                       WorkspaceViewportProfile *profile);

} // namespace pmxer
