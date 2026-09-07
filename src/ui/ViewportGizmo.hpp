#pragma once

#include "../render/Camera.hpp"

#include <imgui.h>

namespace pmxer {

class DocumentSession;

void drawViewportGizmo(DocumentSession &session, const CameraMatrices &camera,
                       ImVec2 origin, ImVec2 size);

} // namespace pmxer
