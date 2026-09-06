#include "WeightTool.hpp"

#include "../EditorOperations.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace pmxer {
namespace {

std::optional<mmd::BoneHandle> mirroredBone(const mmd::PmxDocument &document, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= document.model().bones.size())
        return std::nullopt;
    auto name = document.model().bones[static_cast<std::size_t>(index)].name;
    if (name.size() >= 2 && name.ends_with("_l"))
        name.replace(name.size() - 1, 1, "r");
    else if (name.size() >= 2 && name.ends_with("_r"))
        name.replace(name.size() - 1, 1, "l");
    else {
        const auto left = name.find("左");
        const auto right = name.find("右");
        if (left != std::string::npos)
            name.replace(left, std::string("左").size(), "右");
        else if (right != std::string::npos)
            name.replace(right, std::string("右").size(), "左");
        else
            return document.boneHandle(static_cast<std::size_t>(index));
    }
    for (std::size_t candidate = 0; candidate < document.model().bones.size(); ++candidate)
        if (document.model().bones[candidate].name == name)
            return document.boneHandle(candidate);
    return document.boneHandle(static_cast<std::size_t>(index));
}

} // namespace

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
            if (const auto bone = mirroredBone(session.document, value->bones[i]))
                skin.bones[i] = *bone;
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
