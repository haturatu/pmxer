#include "MorphTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {
namespace {

std::int32_t indexOf(const mmd::PmxDocument &document, mmd::VertexHandle handle) {
    const auto *value = document.resolve(handle);
    return value == nullptr ? -2 : static_cast<std::int32_t>(value - document.model().vertices.data());
}
std::int32_t indexOf(const mmd::PmxDocument &document, mmd::BoneHandle handle) {
    const auto *value = document.resolve(handle);
    return value == nullptr ? -2 : static_cast<std::int32_t>(value - document.model().bones.data());
}
std::int32_t indexOf(const mmd::PmxDocument &document, mmd::MorphHandle handle) {
    const auto *value = document.resolve(handle);
    return value == nullptr ? -2 : static_cast<std::int32_t>(value - document.model().morphs.data());
}
std::int32_t indexOf(const mmd::PmxDocument &document, mmd::RigidBodyHandle handle) {
    const auto *value = document.resolve(handle);
    return value == nullptr ? -2 : static_cast<std::int32_t>(value - document.model().rigidBodies.data());
}

} // namespace

bool setMorphOffset(DocumentSession &session, mmd::MorphHandle handle, std::size_t index, mmd::PmxMorphOffset value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setMorphOffset(handle, index, value);
    }, "モーフオフセットを変更").success;
}

bool addVertexMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::VertexHandle vertex, mmd::Float3 value) {
    const auto index = indexOf(session.document, vertex);
    if (index < 0)
        return false;
    mmd::PmxMorphOffset offset;
    offset.index = index;
    offset.vector3 = value;
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addMorphOffset(handle, offset);
    }, "頂点モーフオフセットを追加").success;
}

bool addBoneMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::BoneHandle bone, mmd::Float3 translation,
                        mmd::Float4 rotation) {
    const auto index = indexOf(session.document, bone);
    if (index < 0)
        return false;
    mmd::PmxMorphOffset offset;
    offset.index = index;
    offset.vector3 = translation;
    offset.vector4 = rotation;
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addMorphOffset(handle, offset);
    }, "ボーンモーフオフセットを追加").success;
}

bool addGroupMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::MorphHandle target, float weight) {
    const auto index = indexOf(session.document, target);
    if (index < 0)
        return false;
    mmd::PmxMorphOffset offset;
    offset.index = index;
    offset.scalar = weight;
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addMorphOffset(handle, offset);
    }, "グループモーフオフセットを追加").success;
}

bool addImpulseMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::RigidBodyHandle body,
                           mmd::Float3 velocity, mmd::Float3 torque, bool local) {
    const auto index = indexOf(session.document, body);
    if (index < 0)
        return false;
    mmd::PmxMorphOffset offset;
    offset.index = index;
    offset.vector3 = velocity;
    offset.tertiaryVector3 = torque;
    offset.local = local;
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addMorphOffset(handle, offset);
    }, "インパルスモーフオフセットを追加").success;
}

} // namespace pmxer

