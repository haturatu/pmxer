#include "ViewportCapabilities.hpp"

#include "DocumentSession.hpp"

namespace pmxer {

bool TransformCapabilities::supports(ViewportTool tool) const noexcept {
    switch (tool) {
    case ViewportTool::select:
    case ViewportTool::move:
        return move;
    case ViewportTool::rotate:
        return rotate;
    case ViewportTool::scale:
        return scale;
    }
    return false;
}

namespace {

TransformCapabilities capabilitiesFor(SelectionKind kind) noexcept {
    switch (kind) {
    case SelectionKind::vertex:
    case SelectionKind::bone:
        return {true, false, false};
    case SelectionKind::rigidBody:
        return {true, true, true};
    case SelectionKind::joint:
        return {true, true, false};
    default:
        return {};
    }
}

} // namespace

TransformCapabilities transformCapabilities(const DocumentSession &session) noexcept {
    if (session.selection.items().empty())
        return {};
    auto result = capabilitiesFor(session.selection.items().front().kind);
    for (const auto &item : session.selection.items()) {
        const auto current = capabilitiesFor(item.kind);
        result.move = result.move && current.move;
        result.rotate = result.rotate && current.rotate;
        result.scale = result.scale && current.scale;
    }
    return result;
}

} // namespace pmxer
