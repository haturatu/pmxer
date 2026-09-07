#pragma once

#include "../editor/Selection.hpp"
#include "../render/Camera.hpp"

#include <imgui.h>
#include <mmd/pmx.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace pmxer {

class DocumentSession;

struct ViewportPickResult {
    SelectionItem item;
    std::size_t index{};
    std::optional<std::size_t> face;
    mmd::Float3 position{};
};

[[nodiscard]] std::optional<ViewportPickResult>
pickViewport(const DocumentSession &session,
             const std::vector<mmd::PmxVertex> &vertices,
             const CameraState &camera, ImVec2 origin, ImVec2 size,
             ImVec2 mouse);

} // namespace pmxer
