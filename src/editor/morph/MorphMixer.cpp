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
                        std::vector<pmxer::morph::MorphData> &parts) {
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
                                   weight * offset.scalar, stack, parts);
        }
    }
    stack[index] = 0U;
}

} // namespace

namespace pmxer::morph {

void syncPreview(DocumentSession &session) {
    session.preview.morphValues = session.deform.blends;
    ++session.preview.morphRevision;
    if (!session.preview.controller)
        return;
    session.preview.controller->clearMorphPreviews();
    for (const auto &blend : session.deform.blends) {
        if (session.document.resolve(blend.morph) == nullptr)
            continue;
        if (session.deform.solo && blend.morph != session.deform.soloMorph)
            continue;
        session.preview.controller->setMorphPreview(blend.morph, blend.weight);
    }
    session.preview.controller->setVertexPreview({});
    session.preview.controller->setBonePreview({});
    session.preview.baseFrame = session.preview.controller->evaluate();
    refreshDeformPreview(session);
    session.preview.appliedMorphRevision = session.preview.morphRevision;
}

void setBlend(DocumentSession &session, mmd::MorphHandle morph, float weight) {
    if (session.document.resolve(morph) == nullptr)
        return;
    const auto found = std::find_if(session.deform.blends.begin(), session.deform.blends.end(),
                                    [&](const auto &blend) { return blend.morph == morph; });
    const auto value = std::clamp(weight, 0.0F, 1.0F);
    if (found == session.deform.blends.end())
        session.deform.blends.push_back({morph, value});
    else
        found->weight = value;
    session.deform.mode = DeformMode::mix;
    syncPreview(session);
}

void clearBlend(DocumentSession &session, mmd::MorphHandle morph) {
    const auto oldSize = session.deform.blends.size();
    std::erase_if(session.deform.blends,
                  [&](const auto &blend) { return blend.morph == morph; });
    if (session.deform.soloMorph == morph) {
        session.deform.solo = false;
        session.deform.soloMorph = {};
    }
    if (oldSize != session.deform.blends.size())
        syncPreview(session);
}

void resetMix(DocumentSession &session) {
    session.deform.blends.clear();
    session.deform.solo = false;
    session.deform.soloMorph = {};
    syncPreview(session);
}

void setSoloMorph(DocumentSession &session, mmd::MorphHandle morph) {
    if (session.document.resolve(morph) == nullptr)
        return;
    session.deform.solo = true;
    session.deform.soloMorph = morph;
    syncPreview(session);
}

void clearSoloMorph(DocumentSession &session) {
    session.deform.solo = false;
    session.deform.soloMorph = {};
    syncPreview(session);
}

OperationResult bakeMixAsVertexMorph(DocumentSession &session, std::string name) {
    std::vector<MorphData> parts;
    std::vector<std::uint8_t> stack(session.document.model().morphs.size());
    for (const auto &blend : session.deform.blends) {
        if (session.deform.solo && blend.morph != session.deform.soloMorph)
            continue;
        const auto *morph = session.document.resolve(blend.morph);
        if (morph == nullptr)
            continue;
        const auto index = static_cast<std::size_t>(morph - session.document.model().morphs.data());
        collectVertexMorph(session.document.model(), index, blend.weight, stack, parts);
    }
    if (parts.empty())
        return {false, "Mixerに頂点モーフがありません"};
    return createMorphFromData(session, combine(parts), std::move(name),
                               "Mixerの状態から頂点モーフを作成");
}

} // namespace pmxer::morph
