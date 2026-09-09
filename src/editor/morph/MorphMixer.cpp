#include "MorphMixer.hpp"

#include "../../preview/PreviewController.hpp"
#include "../DocumentSession.hpp"
#include "../DeformController.hpp"
#include "MorphCapture.hpp"
#include "MorphOps.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

void collectVertexMorph(const mmd::PmxModel &model, std::size_t index, float weight,
                        std::vector<std::uint8_t> &stack,
                        std::vector<pmxer::morph::MorphData> &parts,
                        std::set<std::uint8_t> &ignoredTypes) {
    if (index >= model.morphs.size() || std::abs(weight) <= 1e-7F || stack[index] != 0U)
        return;
    stack[index] = 1U;
    const auto &morph = model.morphs[index];
    if (morph.type == 1U) {
        parts.push_back(pmxer::morph::scale(pmxer::morph::copy(morph), weight));
    } else if (morph.type == 0U || morph.type == 9U) {
        for (const auto &offset : morph.offsets) {
            if (offset.index >= 0)
                collectVertexMorph(model, static_cast<std::size_t>(offset.index),
                                   weight * offset.scalar, stack, parts, ignoredTypes);
        }
    } else {
        ignoredTypes.insert(morph.type);
    }
    stack[index] = 0U;
}

} // namespace

namespace pmxer::morph {

std::vector<MorphBlend> effectiveMorphMix(const DocumentSession &session) {
    std::vector<MorphBlend> result;
    result.reserve(session.preview.morphValues.size());
    for (const auto &blend : session.preview.morphValues) {
        if (session.preview.solo && blend.morph != session.preview.soloMorph)
            continue;
        if (session.document.resolve(blend.morph) != nullptr)
            result.push_back(blend);
    }
    return result;
}

VertexBakeAnalysis analyzeVertexMix(const DocumentSession &session) {
    VertexBakeAnalysis result;
    std::vector<std::uint8_t> stack(session.document.model().morphs.size());
    for (const auto &blend : effectiveMorphMix(session)) {
        const auto *morph = session.document.resolve(blend.morph);
        if (morph == nullptr)
            continue;
        const auto index = static_cast<std::size_t>(morph - session.document.model().morphs.data());
        collectVertexMorph(session.document.model(), index, blend.weight, stack,
                           result.vertexParts, result.ignoredTypes);
    }
    return result;
}

void syncPreview(DocumentSession &session) {
    ++session.preview.morphRevision;
    if (!session.preview.controller)
        return;
    session.preview.controller->clearMorphPreviews();
    for (const auto &blend : effectiveMorphMix(session))
        session.preview.controller->setMorphPreview(blend.morph, blend.weight);
    session.preview.controller->setVertexPreview(
        session.deform.mode == DeformMode::shape ? vertexMorphOffsets(session)
                                                 : std::vector<mmd::PmxMorphOffset>{});
    session.preview.controller->setBonePreview(
        session.deform.mode == DeformMode::pose ? boneMorphOffsets(session)
                                                : std::vector<mmd::PmxMorphOffset>{});
    refreshDeformPreview(session);
    session.preview.appliedMorphRevision = session.preview.morphRevision;
}

void setBlend(DocumentSession &session, mmd::MorphHandle morph, float weight) {
    if (session.document.resolve(morph) == nullptr)
        return;
    const auto found = std::find_if(session.preview.morphValues.begin(), session.preview.morphValues.end(),
                                    [&](const auto &blend) { return blend.morph == morph; });
    const auto value = std::clamp(weight, 0.0F, 1.0F);
    if (found == session.preview.morphValues.end())
        session.preview.morphValues.push_back({morph, value});
    else
        found->weight = value;
    syncPreview(session);
}

void clearBlend(DocumentSession &session, mmd::MorphHandle morph) {
    const auto oldSize = session.preview.morphValues.size();
    std::erase_if(session.preview.morphValues,
                  [&](const auto &blend) { return blend.morph == morph; });
    if (session.preview.soloMorph == morph) {
        session.preview.solo = false;
        session.preview.soloMorph = {};
    }
    if (oldSize != session.preview.morphValues.size())
        syncPreview(session);
}

void resetMix(DocumentSession &session) {
    session.preview.morphValues.clear();
    session.preview.solo = false;
    session.preview.soloMorph = {};
    syncPreview(session);
}

void setSoloMorph(DocumentSession &session, mmd::MorphHandle morph) {
    if (session.document.resolve(morph) == nullptr)
        return;
    session.preview.solo = true;
    session.preview.soloMorph = morph;
    syncPreview(session);
}

void clearSoloMorph(DocumentSession &session) {
    session.preview.solo = false;
    session.preview.soloMorph = {};
    syncPreview(session);
}

OperationResult bakeMixAsVertexMorph(DocumentSession &session, std::string name,
                                     VertexBakeOptions options) {
    const auto analysis = analyzeVertexMix(session);
    if (!analysis.ignoredTypes.empty() && !options.allowIgnoredTypes)
        return {false, "Mixerに頂点以外のモーフ成分があります"};
    if (analysis.vertexParts.empty())
        return {false, "Mixerに頂点モーフがありません"};
    return createMorphFromData(session, combine(analysis.vertexParts), std::move(name),
                               "Mixerの状態から頂点モーフを作成");
}

} // namespace pmxer::morph
