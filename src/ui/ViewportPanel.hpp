#pragma once

#include "../editor/ViewportLighting.hpp"
#include "../editor/DocumentSession.hpp"
#include "../editor/WorkspacePolicy.hpp"

#include <mmd/animation.hpp>

namespace pmxer {

class GpuModelRenderer;

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame,
                       GpuModelRenderer *renderer,
                       bool *showDiagnostics, bool *open,
                       EditorWorkspace &activeWorkspace,
                       WorkspaceViewportProfile *profile,
                       ViewportLightingSettings &lighting);

} // namespace pmxer
