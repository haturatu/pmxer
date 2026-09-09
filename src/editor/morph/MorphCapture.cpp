#include "MorphCapture.hpp"

#include "MorphMixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace pmxer::morph {
namespace {

bool nonZero(const mmd::Float3 &value) {
    return std::abs(value[0]) > 1e-7F || std::abs(value[1]) > 1e-7F || std::abs(value[2]) > 1e-7F;
}

bool nonZeroBoneDelta(const BoneDelta &delta) {
    if (nonZero(delta.translation))
        return true;
    float length{};
    for (const auto component : delta.rotation)
        length += component * component;
    if (length <= 1e-12F)
        return false;
    const auto inverseLength = 1.0F / std::sqrt(length);
    const auto x = delta.rotation[0] * inverseLength;
    const auto y = delta.rotation[1] * inverseLength;
    const auto z = delta.rotation[2] * inverseLength;
    const auto w = delta.rotation[3] * inverseLength;
    return std::abs(x) > 1e-6F || std::abs(y) > 1e-6F || std::abs(z) > 1e-6F ||
           std::abs(std::abs(w) - 1.0F) > 1e-6F;
}

} // namespace

OperationResult createMorphFromData(DocumentSession &session, MorphData data,
                                    std::string name, std::string description,
                                    std::uint8_t panel) {
    data = pruneZeroOffsets(std::move(data));
    if (data.offsets.empty())
        return {false, "モーフオフセットがありません"};
    mmd::MorphHandle created;
    const auto result = applyTransaction(
        session,
        [&](auto &transaction) {
            mmd::PmxMorph morph;
            morph.name = name.empty() ? "New Morph" : std::move(name);
            morph.englishName = data.englishName;
            morph.type = data.type;
            morph.panel = panel == 0U ? data.panel : panel;
            created = transaction.addMorph(std::move(morph));
            if (!created)
                return false;
            for (const auto &offset : data.offsets)
                if (!transaction.addMorphOffset(created, offset))
                    return false;
            return true;
        },
        std::move(description));
    if (result.success)
        session.selection.clear();
    return result;
}

OperationResult captureVertexMorph(DocumentSession &session, std::string name) {
    MorphData data{1U, {}, 4U, {}};
    for (const auto &delta : session.deform.vertices) {
        if (!delta.vertex || !nonZero(delta.offset) || session.document.resolve(delta.vertex) == nullptr)
            continue;
        const auto *vertex = session.document.resolve(delta.vertex);
        mmd::PmxMorphOffset offset;
        offset.index = static_cast<std::int32_t>(vertex - session.document.model().vertices.data());
        offset.vector3 = delta.offset;
        data.offsets.push_back(offset);
    }
    const auto result = createMorphFromData(session, std::move(data), std::move(name),
                                             "編集状態から頂点モーフを作成");
    if (result.success)
        session.deform.clearVertexOverlay();
    return result;
}

OperationResult captureBoneMorph(DocumentSession &session, std::string name) {
    MorphData data{2U, {}, 4U, {}};
    for (const auto &delta : session.deform.bones) {
        if (!nonZeroBoneDelta(delta) || !delta.bone || session.document.resolve(delta.bone) == nullptr)
            continue;
        const auto bone = session.document.resolve(delta.bone);
        if (bone == nullptr)
            continue;
        mmd::PmxMorphOffset offset;
        offset.index = static_cast<std::int32_t>(bone - session.document.model().bones.data());
        offset.vector3 = delta.translation;
        offset.vector4 = delta.rotation;
        data.offsets.push_back(offset);
    }
    const auto result = createMorphFromData(session, std::move(data), std::move(name),
                                             "現在ポーズからボーンモーフを作成");
    if (result.success)
        session.deform.clearBoneOverlay();
    return result;
}

OperationResult captureGroupMorph(DocumentSession &session, std::string name) {
    MorphData data{0U, {}, 4U, {}};
    for (const auto &blend : effectiveMorphMix(session)) {
        if (!blend.morph || std::abs(blend.weight) <= 1e-7F ||
            session.document.resolve(blend.morph) == nullptr)
            continue;
        mmd::PmxMorphOffset offset;
        const auto *morph = session.document.resolve(blend.morph);
        offset.index = static_cast<std::int32_t>(morph - session.document.model().morphs.data());
        offset.scalar = blend.weight;
        data.offsets.push_back(offset);
    }
    return createMorphFromData(session, std::move(data), std::move(name),
                               "Mixerの状態からグループモーフを作成");
}

OperationResult duplicateMorph(DocumentSession &session, mmd::MorphHandle source,
                               std::string name) {
    const auto *morph = session.document.resolve(source);
    if (morph == nullptr)
        return {false, "対象モーフが見つかりません"};
    return createMorphFromData(session, copy(*morph), std::move(name), "モーフを複製");
}

OperationResult bakeAndReverseBase(DocumentSession &session, mmd::MorphHandle source,
                                   BakeReverseOptions options) {
    const auto *morph = session.document.resolve(source);
    if (morph == nullptr || morph->type != 1U)
        return {false, "Bake & Reverse Base は頂点モーフにのみ使用できます"};
    const auto references = session.document.referencesTo(source);
    const auto referencedMorphs = static_cast<std::size_t>(std::count_if(
        references.begin(), references.end(), [](const auto &reference) {
            return reference.ownerKind == mmd::ReferenceObjectKind::morph;
        }));
    if (referencedMorphs != 0U && !options.allowReferencedMorph)
        return {false, "対象モーフは他のGroup/Flipモーフから参照されています"};
    const auto sourceIndex = static_cast<std::size_t>(morph - session.document.model().morphs.data());
    std::vector<mmd::Float3> total(session.document.model().vertices.size());
    for (const auto &offset : morph->offsets) {
        if (offset.index < 0 || static_cast<std::size_t>(offset.index) >= total.size())
            continue;
        for (std::size_t component = 0; component < 3U; ++component)
            total[static_cast<std::size_t>(offset.index)][component] += offset.vector3[component];
    }
    bool changed = false;
    for (const auto &delta : total)
        changed = changed || nonZero(delta);
    if (!changed)
        return {false, "対象モーフに変形量がありません"};

    const auto result = applyTransaction(
        session,
        [&](auto &transaction) {
            const auto &model = session.document.model();
            for (std::size_t vertexIndex = 0; vertexIndex < total.size(); ++vertexIndex) {
                if (!nonZero(total[vertexIndex]))
                    continue;
                auto position = model.vertices[vertexIndex].position;
                for (std::size_t component = 0; component < 3U; ++component)
                    position[component] += total[vertexIndex][component];
                if (!transaction.setVertexPosition(session.document.vertexHandle(vertexIndex), position))
                    return false;
            }
            for (std::size_t morphIndex = 0; morphIndex < model.morphs.size(); ++morphIndex) {
                const auto handle = session.document.morphHandle(morphIndex);
                const auto &current = model.morphs[morphIndex];
                if (current.type != 1U)
                    continue;
                std::vector<bool> rebased(total.size());
                std::vector<bool> present(total.size());
                for (std::size_t offsetIndex = 0; offsetIndex < current.offsets.size(); ++offsetIndex) {
                    auto offset = current.offsets[offsetIndex];
                    if (offset.index < 0 || static_cast<std::size_t>(offset.index) >= total.size())
                        continue;
                    const auto vertexIndex = static_cast<std::size_t>(offset.index);
                    present[vertexIndex] = true;
                    if (morphIndex == sourceIndex) {
                        for (auto &component : offset.vector3)
                            component = -component;
                    } else if (!rebased[vertexIndex]) {
                        for (std::size_t component = 0; component < 3U; ++component)
                            offset.vector3[component] -= total[vertexIndex][component];
                        rebased[vertexIndex] = true;
                    }
                    if (!transaction.setMorphOffset(handle, offsetIndex, offset))
                        return false;
                }
                if (morphIndex == sourceIndex)
                    continue;
                // Vertex morphs are sparse. An omitted offset means the
                // vertex stays at the base position, so rebasing the base
                // requires inserting -D for every touched, absent vertex.
                for (std::size_t vertexIndex = 0; vertexIndex < total.size(); ++vertexIndex) {
                    if (present[vertexIndex] || !nonZero(total[vertexIndex]))
                        continue;
                    mmd::PmxMorphOffset offset;
                    offset.index = static_cast<std::int32_t>(vertexIndex);
                    for (std::size_t component = 0; component < 3U; ++component)
                        offset.vector3[component] = -total[vertexIndex][component];
                    if (!transaction.addMorphOffset(handle, offset))
                        return false;
                }
            }
            return true;
        },
        "モーフをBake & Reverse Base");
    return result;
}

OperationResult createSideSplitMorphs(DocumentSession &session, MorphData left,
                                      MorphData right, std::string name) {
    if (left.offsets.empty() && right.offsets.empty())
        return {false, "左右どちらにもモーフオフセットがありません"};
    const auto baseName = name.empty() ? std::string{"Morph"} : std::move(name);
    return applyTransaction(
        session,
        [&](auto &transaction) {
            if (!left.offsets.empty()) {
                mmd::PmxMorph morph;
                morph.name = baseName + " Left";
                morph.type = left.type;
                morph.panel = left.panel;
                morph.englishName = left.englishName;
                const auto handle = transaction.addMorph(std::move(morph));
                if (!handle)
                    return false;
                for (const auto &offset : left.offsets)
                    if (!transaction.addMorphOffset(handle, offset))
                        return false;
            }
            if (!right.offsets.empty()) {
                mmd::PmxMorph morph;
                morph.name = baseName + " Right";
                morph.type = right.type;
                morph.panel = right.panel;
                morph.englishName = right.englishName;
                const auto handle = transaction.addMorph(std::move(morph));
                if (!handle)
                    return false;
                for (const auto &offset : right.offsets)
                    if (!transaction.addMorphOffset(handle, offset))
                        return false;
            }
            return true;
        },
        "モーフを左右に分割");
}

} // namespace pmxer::morph
