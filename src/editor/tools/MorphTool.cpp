#include "i18n/Language.hpp"

#include "MorphTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {
bool setMorphOffset(DocumentSession &session, mmd::MorphHandle handle, std::size_t index, mmd::PmxMorphOffset value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setMorphOffset(handle, index, value);
    }, i18n::tr("モーフオフセットを変更")).success;
}

bool addVertexMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::VertexHandle vertex, mmd::Float3 value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addVertexMorphOffset(handle, vertex, value);
    }, i18n::tr("頂点モーフオフセットを追加")).success;
}

bool addBoneMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::BoneHandle bone, mmd::Float3 translation,
                        mmd::Float4 rotation) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addBoneMorphOffset(handle, bone, translation, rotation);
    }, i18n::tr("ボーンモーフオフセットを追加")).success;
}

bool addGroupMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::MorphHandle target, float weight) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addGroupMorphOffset(handle, target, weight);
    }, i18n::tr("グループモーフオフセットを追加")).success;
}

bool addUvMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::VertexHandle vertex, std::uint32_t channel,
                      mmd::Float4 value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addUvMorphOffset(handle, vertex, channel, value);
    }, i18n::tr("UVモーフオフセットを追加")).success;
}

bool addMaterialMorphOffset(DocumentSession &session, mmd::MorphHandle handle,
                            std::optional<mmd::MaterialHandle> material, std::uint8_t operation,
                            std::array<mmd::Float4, 8> values) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addMaterialMorphOffset(handle, material, operation, values);
    }, i18n::tr("材質モーフオフセットを追加")).success;
}

bool addFlipMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::MorphHandle target, float weight) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addFlipMorphOffset(handle, target, weight);
    }, i18n::tr("フリップモーフオフセットを追加")).success;
}

bool addImpulseMorphOffset(DocumentSession &session, mmd::MorphHandle handle, mmd::RigidBodyHandle body,
                           mmd::Float3 velocity, mmd::Float3 torque, bool local) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.addImpulseMorphOffset(handle, body, velocity, torque, local);
    }, i18n::tr("インパルスモーフオフセットを追加")).success;
}

} // namespace pmxer
