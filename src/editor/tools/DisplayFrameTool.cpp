#include "DisplayFrameTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool addBoneToDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle frame, mmd::BoneHandle bone) {
    const auto *value = session.document.resolve(bone);
    if (value == nullptr)
        return false;
    const auto index = static_cast<std::int32_t>(value - session.document.model().bones.data());
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addDisplayFrameItem(frame, {true, index});
    }, "表示枠にボーンを追加").success;
}

bool addMorphToDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle frame, mmd::MorphHandle morph) {
    const auto *value = session.document.resolve(morph);
    if (value == nullptr)
        return false;
    const auto index = static_cast<std::int32_t>(value - session.document.model().morphs.data());
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addDisplayFrameItem(frame, {false, index});
    }, "表示枠にモーフを追加").success;
}

bool moveDisplayItem(DocumentSession &session, mmd::DisplayFrameHandle frame, std::size_t from, std::size_t to) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.moveDisplayFrameItem(frame, from, to);
    }, "表示枠項目を移動").success;
}

} // namespace pmxer

