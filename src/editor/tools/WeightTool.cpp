#include "WeightTool.hpp"

#include "../EditorOperations.hpp"

#include <algorithm>

namespace pmxer {

bool assignBone(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles, mmd::BoneHandle bone,
                std::size_t slot) {
    if (slot >= 4 || session.document.resolve(bone) == nullptr)
        return false;
    return applyTransaction(session, [&](auto &transaction) {
        for (const auto handle : handles) {
            const auto *source = session.document.resolve(handle);
            if (source == nullptr)
                return false;
            mmd::PmxVertexSkin value;
            value.type = source->weightType;
            value.weights = source->weights;
            value.sdefC = source->sdefC;
            value.sdefR0 = source->sdefR0;
            value.sdefR1 = source->sdefR1;
            for (std::size_t i = 0; i < 4; ++i)
                if (source->bones[i] >= 0 && static_cast<std::size_t>(source->bones[i]) < session.document.model().bones.size())
                    value.bones[i] = session.document.boneHandle(static_cast<std::size_t>(source->bones[i]));
            value.bones[slot] = bone;
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
    std::vector<mmd::PmxVertexSkin> values;
    values.reserve(sourceHandles.size());
    for (const auto handle : sourceHandles) {
        const auto *value = session.document.resolve(handle);
        if (value == nullptr)
            return false;
        mmd::PmxVertexSkin skin;
        skin.type = value->weightType;
        skin.weights = value->weights;
        skin.sdefC = value->sdefC;
        skin.sdefR0 = value->sdefR0;
        skin.sdefR1 = value->sdefR1;
        for (std::size_t i = 0; i < 4; ++i)
            if (value->bones[i] >= 0 && static_cast<std::size_t>(value->bones[i]) < session.document.model().bones.size())
                skin.bones[i] = session.document.boneHandle(static_cast<std::size_t>(value->bones[i]));
        values.push_back(skin);
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
