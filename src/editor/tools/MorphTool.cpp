#include "MorphTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {
bool setMorphOffset(DocumentSession &session, mmd::MorphHandle handle, std::size_t index, mmd::PmxMorphOffset value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setMorphOffset(handle, index, value);
    }, "モーフオフセットを変更").success;
}

bool addVertexMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::VertexHandle vertex, mmd::Float3 value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addVertexMorphOffset(handle, vertex, value);
    }, "頂点モーフオフセットを追加").success;
}

bool addBoneMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::BoneHandle bone, mmd::Float3 translation,
                        mmd::Float4 rotation) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addBoneMorphOffset(handle, bone, translation, rotation);
    }, "ボーンモーフオフセットを追加").success;
}

bool addGroupMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::MorphHandle target, float weight) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addGroupMorphOffset(handle, target, weight);
    }, "グループモーフオフセットを追加").success;
}

bool addImpulseMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::RigidBodyHandle body,
                           mmd::Float3 velocity, mmd::Float3 torque, bool local) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addImpulseMorphOffset(handle, body, velocity, torque, local);
    }, "インパルスモーフオフセットを追加").success;
}

} // namespace pmxer
