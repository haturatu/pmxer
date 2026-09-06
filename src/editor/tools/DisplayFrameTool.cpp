#include "DisplayFrameTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool addBoneToDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle frame, mmd::BoneHandle bone) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addDisplayFrameItem(frame, bone);
    }, "表示枠にボーンを追加").success;
}

bool addMorphToDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle frame, mmd::MorphHandle morph) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addDisplayFrameItem(frame, morph);
    }, "表示枠にモーフを追加").success;
}

bool moveDisplayItem(DocumentSession &session, mmd::DisplayFrameHandle frame, std::size_t from, std::size_t to) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.moveDisplayFrameItem(frame, from, to);
    }, "表示枠項目を移動").success;
}

} // namespace pmxer
