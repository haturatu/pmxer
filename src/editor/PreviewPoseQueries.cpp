#include "PreviewPoseQueries.hpp"

#include "DocumentSession.hpp"

namespace pmxer {
namespace {

mmd::Float3 add(const mmd::Float3 &lhs, const mmd::Float3 &rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

mmd::Float3 rotate(const mmd::Float4 &quaternion, const mmd::Float3 &value) {
    const mmd::Float3 q{quaternion[0], quaternion[1], quaternion[2]};
    const auto uv = mmd::Float3{
        q[1] * value[2] - q[2] * value[1],
        q[2] * value[0] - q[0] * value[2],
        q[0] * value[1] - q[1] * value[0],
    };
    const auto uuv = mmd::Float3{
        q[1] * uv[2] - q[2] * uv[1],
        q[2] * uv[0] - q[0] * uv[2],
        q[0] * uv[1] - q[1] * uv[0],
    };
    const auto factor = 2.0F * quaternion[3];
    return {value[0] + uv[0] * factor + uuv[0] * 2.0F,
            value[1] + uv[1] * factor + uuv[1] * 2.0F,
            value[2] + uv[2] * factor + uuv[2] * 2.0F};
}

} // namespace

mmd::Float3 evaluatedBonePosition(const DocumentSession &session, std::size_t index,
                                  const mmd::AnimatedModelFrame *frame) noexcept {
    const auto &model = session.document.model();
    if (index >= model.bones.size())
        return {};
    const auto &bind = model.bones[index].position;
    if (frame == nullptr || index >= frame->bones.size())
        return bind;
    const auto &transform = frame->bones[index];
    return add(rotate(transform.rotation, bind), transform.translation);
}

mmd::Float3 evaluatedBonePosition(const DocumentSession &session, mmd::BoneHandle bone,
                                  const mmd::AnimatedModelFrame *frame) noexcept {
    const auto *value = session.document.resolve(bone);
    if (value == nullptr)
        return {};
    const auto index = static_cast<std::size_t>(value - session.document.model().bones.data());
    return evaluatedBonePosition(session, index, frame);
}

mmd::Float4 evaluatedBoneRotation(const DocumentSession &session, std::size_t index,
                                  const mmd::AnimatedModelFrame *frame) noexcept {
    if (index >= session.document.model().bones.size() || frame == nullptr ||
        index >= frame->bones.size())
        return {0.0F, 0.0F, 0.0F, 1.0F};
    return frame->bones[index].rotation;
}

mmd::Float4 evaluatedBoneRotation(const DocumentSession &session, mmd::BoneHandle bone,
                                  const mmd::AnimatedModelFrame *frame) noexcept {
    const auto *value = session.document.resolve(bone);
    if (value == nullptr)
        return {0.0F, 0.0F, 0.0F, 1.0F};
    const auto index = static_cast<std::size_t>(value - session.document.model().bones.data());
    return evaluatedBoneRotation(session, index, frame);
}

} // namespace pmxer
