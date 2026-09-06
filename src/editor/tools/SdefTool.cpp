#include "SdefTool.hpp"

#include "../EditorOperations.hpp"

#include <cmath>

namespace pmxer {

SdefReport convertBdef2ToSdef(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles) {
    SdefReport report;
    struct Edit {
        mmd::VertexHandle handle;
        mmd::PmxVertexSkin value;
    };
    std::vector<Edit> edits;
    for (const auto handle : handles) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr || source->weightType != mmd::PmxWeightType::bdef2) {
            ++report.rejected;
            continue;
        }
        mmd::PmxVertexSkin value;
        value.type = mmd::PmxWeightType::sdef;
        value.weights = source->weights;
        const auto &bones = session.document.model().bones;
        if (source->bones[0] < 0 || source->bones[1] < 0 || static_cast<std::size_t>(source->bones[0]) >= bones.size() ||
            static_cast<std::size_t>(source->bones[1]) >= bones.size()) {
            ++report.rejected;
            continue;
        }
        const auto first = source->bones[0] >= 0 && static_cast<std::size_t>(source->bones[0]) < bones.size()
                               ? bones[static_cast<std::size_t>(source->bones[0])].position
                               : mmd::Float3{};
        const auto second = source->bones[1] >= 0 && static_cast<std::size_t>(source->bones[1]) < bones.size()
                                ? bones[static_cast<std::size_t>(source->bones[1])].position
                                : first;
        for (std::size_t i = 0; i < 2; ++i)
            value.bones[i] = session.document.boneHandle(static_cast<std::size_t>(source->bones[i]));
        for (std::size_t i = 0; i < 3; ++i) {
            value.sdefC[i] = (first[i] + second[i]) * 0.5F;
            value.sdefR0[i] = first[i];
            value.sdefR1[i] = second[i];
        }
        edits.push_back({handle, value});
    }
    const auto result = applyTransaction(session, [&](auto &transaction) {
        for (const auto &edit : edits)
            if (!transaction.setVertexSkin(edit.handle, edit.value))
                return false;
        return true;
    }, "BDEF2からSDEFへ変換");
    if (result.success)
        report.converted = edits.size();
    else
        report.rejected += edits.size();
    return report;
}

SdefReport convertSdefToBdef2(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles) {
    SdefReport report;
    struct Edit {
        mmd::VertexHandle handle;
        mmd::PmxVertexSkin value;
    };
    std::vector<Edit> edits;
    for (const auto handle : handles) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr || source->weightType != mmd::PmxWeightType::sdef) {
            ++report.rejected;
            continue;
        }
        mmd::PmxVertexSkin value;
        value.type = mmd::PmxWeightType::bdef2;
        value.weights = source->weights;
        value.sdefC = source->sdefC;
        value.sdefR0 = source->sdefR0;
        value.sdefR1 = source->sdefR1;
        for (std::size_t i = 0; i < 2; ++i) {
            if (source->bones[i] < 0 || static_cast<std::size_t>(source->bones[i]) >= session.document.model().bones.size()) {
                ++report.rejected;
                value.bones[i] = {};
            } else {
                value.bones[i] = session.document.boneHandle(static_cast<std::size_t>(source->bones[i]));
            }
        }
        edits.push_back({handle, value});
    }
    const auto result = applyTransaction(session, [&](auto &transaction) {
        for (const auto &edit : edits)
            if (!transaction.setVertexSkin(edit.handle, edit.value))
                return false;
        return true;
    }, "SDEFからBDEF2へ変換");
    if (result.success)
        report.converted = edits.size();
    else
        report.rejected += edits.size();
    return report;
}

bool mirrorSdef(DocumentSession &session, const std::vector<mmd::VertexHandle> &handles) {
    struct Edit {
        mmd::VertexHandle handle;
        mmd::PmxVertexSkin value;
    };
    std::vector<Edit> edits;
    for (const auto handle : handles) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr || source->weightType != mmd::PmxWeightType::sdef)
            continue;
        mmd::PmxVertexSkin value;
        value.type = source->weightType;
        value.weights = source->weights;
        value.sdefC = source->sdefC;
        value.sdefR0 = source->sdefR0;
        value.sdefR1 = source->sdefR1;
        for (std::size_t i = 0; i < 4; ++i)
            if (source->bones[i] >= 0 && static_cast<std::size_t>(source->bones[i]) < session.document.model().bones.size())
                value.bones[i] = session.document.boneHandle(static_cast<std::size_t>(source->bones[i]));
        value.sdefC[0] = -value.sdefC[0];
        value.sdefR0[0] = -value.sdefR0[0];
        value.sdefR1[0] = -value.sdefR1[0];
        edits.push_back({handle, value});
    }
    return applyTransaction(session, [&](auto &transaction) {
        for (const auto &edit : edits)
            if (!transaction.setVertexSkin(edit.handle, edit.value))
                return false;
        return true;
    }, "SDEFをミラー").success;
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
