#include "WeightTool.hpp"

#include "../EditorOperations.hpp"

#include <algorithm>

namespace pmxer {

bool assignBone(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles, mmd::BoneHandle bone,
                std::size_t slot) {
    if (slot >= 4 || session.document.resolve(bone) == nullptr)
        return false;
    const auto *bonePointer = session.document.resolve(bone);
    const auto &bones = session.document.model().bones;
    const auto boneIndex = static_cast<std::int32_t>(bonePointer - bones.data());
    return applyTransaction(session, [&](auto &transaction) {
        for (const auto handle : handles) {
            const auto *source = session.document.resolve(handle);
            if (source == nullptr)
                return false;
            auto value = *source;
            value.bones[slot] = boneIndex;
            if (!transaction.setVertexSkin(handle, value))
                return false;
        }
        return true;
    }, "ボーンウェイトを割り当て").success;
}

bool mirrorWeights(DocumentSession &session, const std::vector<mmd::VertexHandle> &sourceHandles,
                   const std::vector<mmd::VertexHandle> &destinationHandles) {
    if (sourceHandles.size() != destinationHandles.size())
        return false;
    std::vector<mmd::PmxVertex> values;
    values.reserve(sourceHandles.size());
    for (const auto handle : sourceHandles) {
        const auto *value = session.document.resolve(handle);
        if (value == nullptr)
            return false;
        values.push_back(*value);
    }
    return applyTransaction(session, [&](auto &transaction) {
        for (std::size_t i = 0; i < values.size(); ++i)
            if (!transaction.setVertexSkin(destinationHandles[i], values[i]))
                return false;
        return true;
    }, "ウェイトをミラー").success;
}

bool pruneWeights(DocumentSession &session, float threshold) {
    return normalizeWeights(session, std::max(0.0F, threshold)).success;
}

} // namespace pmxer

