#include "BoneTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool setBoneParent(DocumentSession &session, mmd::BoneHandle child, mmd::BoneHandle parent) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setBoneParent(child, parent);
    }, "親ボーンを変更").success;
}

bool setBoneIkTarget(DocumentSession &session, mmd::BoneHandle bone, mmd::BoneHandle target) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setBoneIkTarget(bone, target);
    }, "IKターゲットを変更").success;
}

bool reorderBone(DocumentSession &session, mmd::BoneHandle bone, std::size_t destination) {
    return moveBone(session, bone, destination).success;
}

bool addIkLink(DocumentSession &session, mmd::BoneHandle bone, mmd::PmxIkLink link) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addBoneIkLink(bone, link);
    }, "IKリンクを追加").success;
}

} // namespace pmxer
