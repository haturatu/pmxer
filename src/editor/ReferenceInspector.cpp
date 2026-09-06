#include "ReferenceInspector.hpp"

namespace pmxer {
namespace {

void add(ReferenceSummary &summary, mmd::ReferenceObjectKind kind) {
    switch (kind) {
    case mmd::ReferenceObjectKind::vertex:
        ++summary.vertices;
        break;
    case mmd::ReferenceObjectKind::bone:
        ++summary.childBones;
        break;
    case mmd::ReferenceObjectKind::morph:
        ++summary.morphs;
        break;
    case mmd::ReferenceObjectKind::displayFrame:
        ++summary.displayFrames;
        break;
    case mmd::ReferenceObjectKind::rigidBody:
        ++summary.rigidBodies;
        break;
    case mmd::ReferenceObjectKind::joint:
        ++summary.joints;
        break;
    case mmd::ReferenceObjectKind::softBody:
        ++summary.softBodies;
        break;
    default:
        break;
    }
}

template <typename Handle>
ReferenceSummary summarize(const mmd::PmxDocument &document, Handle handle) {
    ReferenceSummary summary;
    for (const auto &site : document.referencesTo(handle))
        add(summary, site.ownerKind);
    return summary;
}

} // namespace

ReferenceSummary summarizeReferences(const mmd::PmxDocument &document, mmd::BoneHandle handle) {
    return summarize(document, handle);
}

ReferenceSummary summarizeReferences(const mmd::PmxDocument &document, mmd::MaterialHandle handle) {
    return summarize(document, handle);
}

ReferenceSummary summarizeReferences(const mmd::PmxDocument &document, mmd::TextureHandle handle) {
    return summarize(document, handle);
}

} // namespace pmxer

