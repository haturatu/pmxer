#pragma once

#include "../editor/DocumentSession.hpp"
#include "../editor/WorkspacePolicy.hpp"

#include <mmd/animation.hpp>

namespace pmxer {

class GpuModelRenderer;

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame = nullptr,
                       GpuModelRenderer *renderer = nullptr,
                       bool *showDiagnostics = nullptr, bool *open = nullptr,
                       EditorWorkspace &activeWorkspace,
                       WorkspaceViewportProfile *profile);

} // namespace pmxer
