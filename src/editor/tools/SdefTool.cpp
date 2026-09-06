#include "SdefTool.hpp"

#include "../EditorOperations.hpp"

#include <cmath>

namespace pmxer {

SdefReport convertBdef2ToSdef(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles) {
    SdefReport report;
    for (const auto handle : handles) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr || source->weightType != mmd::PmxWeightType::bdef2) {
            ++report.rejected;
            continue;
        }
        auto value = *source;
        value.weightType = mmd::PmxWeightType::sdef;
        const auto &bones = session.document.model().bones;
        const auto first = value.bones[0] >= 0 && static_cast<std::size_t>(value.bones[0]) < bones.size()
                               ? bones[static_cast<std::size_t>(value.bones[0])].position
                               : mmd::Float3{};
        const auto second = value.bones[1] >= 0 && static_cast<std::size_t>(value.bones[1]) < bones.size()
                                ? bones[static_cast<std::size_t>(value.bones[1])].position
                                : first;
        for (std::size_t i = 0; i < 3; ++i) {
            value.sdefC[i] = (first[i] + second[i]) * 0.5F;
            value.sdefR0[i] = first[i];
            value.sdefR1[i] = second[i];
        }
        if (editVertex(session, handle, value).success)
            ++report.converted;
        else
            ++report.rejected;
    }
    return report;
}

SdefReport convertSdefToBdef2(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles) {
    SdefReport report;
    for (const auto handle : handles) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr || source->weightType != mmd::PmxWeightType::sdef) {
            ++report.rejected;
            continue;
        }
        auto value = *source;
        value.weightType = mmd::PmxWeightType::bdef2;
        if (editVertex(session, handle, value).success)
            ++report.converted;
        else
            ++report.rejected;
    }
    return report;
}

bool mirrorSdef(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles) {
    bool changed = false;
    for (const auto handle : handles) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr || source->weightType != mmd::PmxWeightType::sdef)
            continue;
        auto value = *source;
        value.sdefC[0] = -value.sdefC[0];
        value.sdefR0[0] = -value.sdefR0[0];
        value.sdefR1[0] = -value.sdefR1[0];
        changed = editVertex(session, handle, value).success || changed;
    }
    return changed;
}

bool hasSuspiciousSdef(const mmd::PmxVertex &vertex) noexcept {
    if (vertex.weightType != mmd::PmxWeightType::sdef)
        return false;
    for (const auto &value : vertex.sdefC)
        if (!std::isfinite(value))
            return true;
    for (const auto &value : vertex.sdefR0)
        if (!std::isfinite(value))
            return true;
    for (const auto &value : vertex.sdefR1)
        if (!std::isfinite(value))
            return true;
    return false;
}

} // namespace pmxer

