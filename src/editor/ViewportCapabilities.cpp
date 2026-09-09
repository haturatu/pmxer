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
    if (session.deform.mode == DeformMode::mix)
        return {};
    if (session.deform.active() && session.deform.mode == DeformMode::shape) {
        for (const auto &item : session.selection.items())
            if (item.kind != SelectionKind::vertex)
                return {};
        return {true, true, true};
    }
    if (session.deform.active() && session.deform.mode == DeformMode::pose) {
        for (const auto &item : session.selection.items())
            if (item.kind != SelectionKind::bone)
                return {};
        return {true, true, false};
    }
    auto result = capabilitiesFor(session.selection.items().front().kind);
    for (const auto &item : session.selection.items()) {
        const auto current = capabilitiesFor(item.kind);
        result.move = result.move && current.move;
        result.rotate = result.rotate && current.rotate;
        result.scale = result.scale && current.scale;
    }
    return result;
}

ActionAvailability actionAvailability(EditorAction action,
                                      const DocumentSession &session) noexcept {
    const auto capabilities = transformCapabilities(session);
    switch (action) {
    case EditorAction::viewportMove:
        return {capabilities.move, capabilities.move ? SupportLevel::supported
                                                       : SupportLevel::unsupported,
                "選択対象は移動編集に対応していません"};
    case EditorAction::viewportRotate:
        return {capabilities.rotate, capabilities.rotate ? SupportLevel::supported
                                                          : SupportLevel::unsupported,
                "選択対象は回転編集に対応していません"};
    case EditorAction::viewportScale:
        return {capabilities.scale, capabilities.scale ? SupportLevel::supported
                                                        : SupportLevel::unsupported,
                "選択対象は拡縮編集に対応していません"};
    }
    return {};
}

} // namespace pmxer
