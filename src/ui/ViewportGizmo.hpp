#pragma once

#include "../render/Camera.hpp"
#include "../editor/ViewportCapabilities.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include <optional>

namespace pmxer {

class DocumentSession;

[[nodiscard]] std::optional<ImGuizmo::OPERATION>
operationFor(ViewportTool tool, const TransformCapabilities &capabilities) noexcept;

void drawViewportGizmo(DocumentSession &session, const CameraMatrices &camera,
                       ImVec2 origin, ImVec2 size);

} // namespace pmxer
