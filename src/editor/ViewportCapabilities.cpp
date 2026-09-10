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

constexpr std::uint16_t ikFlag = 0x0020U;
constexpr std::uint16_t appendFlags = 0x0300U;
constexpr std::uint16_t afterPhysicsFlag = 0x1000U;

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

bool deformBoneTransformAllowed(const DocumentSession &session,
                                mmd::BoneHandle handle) noexcept {
    return boneTransformAvailability(session, handle).enabled;
}

ActionAvailability boneTransformAvailability(const DocumentSession &session,
                                              mmd::BoneHandle handle) noexcept {
    const auto *bone = session.document.resolve(handle);
    if (bone == nullptr)
        return {false, SupportLevel::unsupported, "ボーンが見つかりません"};
    const auto &model = session.document.model();
    const auto index = static_cast<std::size_t>(bone - model.bones.data());
    if ((bone->flags & ikFlag) != 0U)
        return {false, SupportLevel::unsupported,
                "このボーンはIKコントローラーのため変形できません"};
    if ((bone->flags & appendFlags) != 0U)
        return {false, SupportLevel::unsupported,
                "このボーンは付与（Append / Inherit）により制御されているため変形できません"};
    if ((bone->flags & afterPhysicsFlag) != 0U)
        return {false, SupportLevel::unsupported,
                "このボーンは物理後変形（After Physics）のため変形できません"};

    for (const auto &candidate : model.bones) {
        if ((candidate.flags & ikFlag) == 0U)
            continue;
        if (candidate.ikTarget == static_cast<std::int32_t>(index))
            return {false, SupportLevel::unsupported,
                    "このボーンはIKターゲットのため変形できません"};
        for (const auto &link : candidate.ikLinks)
            if (link.bone == static_cast<std::int32_t>(index))
                return {false, SupportLevel::unsupported,
                        "このボーンはIKリンクのため変形できません"};
    }
    for (const auto &body : model.rigidBodies) {
        if (body.physicsEnabled && body.mode != 0U && body.bone == static_cast<std::int32_t>(index))
            return {false, SupportLevel::unsupported,
                    "このボーンは剛体物理により制御されているため変形できません"};
    }
    return {true, SupportLevel::supported, {}};
}

TransformCapabilities transformCapabilities(const DocumentSession &session) noexcept {
    if (session.selection.items().empty())
        return {};
    if (session.deform.active() && session.deform.mode == DeformMode::shape) {
        for (const auto &item : session.selection.items())
            if (item.kind != SelectionKind::vertex)
                return {};
        return {true, true, true};
    }
    if (session.deform.active() && session.deform.mode == DeformMode::pose) {
        if (session.selection.items().size() != 1U)
            return {};
        for (const auto &item : session.selection.items())
            if (item.kind != SelectionKind::bone)
                return {};
        const auto handle = selectionHandle<mmd::BoneTag>(
            session.document, session.selection.items().front());
        if (!deformBoneTransformAllowed(session, handle))
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
