#include "EditGuards.hpp"
#include "EditPipeline.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
namespace pmxer::editing {
namespace {
[[noreturn]] void reject(const char *message) { throw PlanError("guard_failed", message); }
template <class H> H inDomain(H handle, const mmd::PmxDocument &doc) {
    handle.domain = doc.domain();
    return handle;
}
bool sameFace(const mmd::PmxFace &a, const mmd::PmxFace &b) {
    for (std::size_t i = 0; i < 3; ++i)
        if (a.vertices[i].id != b.vertices[i].id ||
            a.vertices[i].generation != b.vertices[i].generation)
            return false;
    return true;
}
bool near(float a, float b) {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <= 1e-5F * std::max({1.0F, std::abs(a), std::abs(b)});
}
using Influences = std::map<std::int32_t, float>;
Influences influences(const mmd::PmxVertex &v) {
    Influences out;
    for (std::size_t i = 0; i < 4; ++i)
        if (v.bones[i] >= 0 && v.weights[i] > 0)
            out[v.bones[i]] += v.weights[i];
    return out;
}
void morphs(const mmd::PmxDocument &before, const mmd::PmxDocument &after,
            const ai::EditTrace &trace) {
    if (before.model().morphs.size() != after.model().morphs.size())
        reject("Morph count changed");
    std::set<std::uint64_t> originalIds;
    for (std::size_t v = 0; v < before.model().vertices.size(); ++v)
        originalIds.insert(before.vertexHandle(v).id);
    std::map<std::uint64_t, std::size_t> materialIndices;
    for (std::size_t m = 0; m < before.model().materials.size(); ++m)
        materialIndices[before.materialHandle(m).id] = m;
    for (std::size_t i = 0; i < before.model().morphs.size(); ++i) {
        const auto &a = before.model().morphs[i];
        const auto *b = after.resolve(inDomain(before.morphHandle(i), after));
        if (!b)
            reject("Morph was removed");
        auto metadata = *b;
        metadata.offsets = a.offsets;
        if (metadata != a)
            reject("Morph metadata changed");
        if (a.offsets.empty() && b->offsets.empty())
            continue;
        if (a.type == 8) {
            for (std::size_t m = 0; m < after.model().materials.size(); ++m) {
                auto origin = after.materialHandle(m).id;
                std::set<std::uint64_t> seen;
                while (trace.materialSources.contains(origin)) {
                    if (!seen.insert(origin).second)
                        reject("Cyclic material provenance");
                    origin = trace.materialSources.at(origin);
                }
                if (!materialIndices.contains(origin))
                    reject("New material has no morph provenance");
                const auto original = materialIndices.at(origin);
                auto sequence = [](const auto &morph, std::size_t index) {
                    std::vector<mmd::PmxMorphOffset> offsets;
                    for (auto value : morph.offsets)
                        if (value.index == -1 || value.index == static_cast<std::int32_t>(index)) {
                            value.index = 0;
                            offsets.push_back(value);
                        }
                    return offsets;
                };
                if (sequence(a, original) != sequence(*b, m))
                    reject("Material morph effect changed");
            }
        } else if (a.type == 1 || (a.type >= 3 && a.type <= 7)) {
            using Value = std::array<float, 4>;
            std::map<std::uint64_t, Value> expected, actual;
            auto collect = [](const auto &morph, const auto &doc, auto &result) {
                for (const auto &offset : morph.offsets) {
                    auto &v = result[doc.vertexHandle(static_cast<std::size_t>(offset.index)).id];
                    if (morph.type == 1)
                        for (std::size_t axis = 0; axis < 3; ++axis)
                            v[axis] += offset.vector3[axis];
                    else
                        for (std::size_t axis = 0; axis < 4; ++axis)
                            v[axis] += offset.vector4[axis];
                }
            };
            collect(a, before, expected);
            collect(*b, after, actual);
            std::set<std::uint64_t> visiting;
            std::function<Value(std::uint64_t)> inherited = [&](std::uint64_t id) -> Value {
                if (expected.contains(id))
                    return expected.at(id);
                if (originalIds.contains(id))
                    return {};
                if (!trace.vertexSources.contains(id) || !visiting.insert(id).second)
                    reject("New vertex has no morph provenance");
                Value result{};
                for (const auto &[parent, weight] : trace.vertexSources.at(id)) {
                    const auto p = inherited(parent);
                    for (std::size_t axis = 0; axis < 4; ++axis)
                        result[axis] += p[axis] * weight;
                }
                visiting.erase(id);
                expected[id] = result;
                return result;
            };
            for (std::size_t v = 0; v < after.model().vertices.size(); ++v) {
                const auto id = after.vertexHandle(v).id;
                const auto wanted = inherited(id);
                const auto actualValue = actual.contains(id) ? actual.at(id) : Value{};
                for (std::size_t axis = 0; axis < 4; ++axis)
                    if (!near(wanted[axis], actualValue[axis]))
                        reject("Vertex or UV morph effect changed");
            }
            for (std::size_t v = 0; v < before.model().vertices.size(); ++v)
                if (!after.resolve(inDomain(before.vertexHandle(v), after)))
                    reject("Morph vertex was removed");
        } else if (a != *b)
            reject("Morph changed");
    }
}
} // namespace
bool sameConnectivity(const mmd::PmxDocument &before, const mmd::PmxDocument &after) {
    if (before.model().vertices.size() != after.model().vertices.size() ||
        before.faces().size() != after.faces().size())
        return false;
    for (std::size_t i = 0; i < before.model().vertices.size(); ++i)
        if (!after.resolve(inDomain(before.vertexHandle(i), after)))
            return false;
    for (std::size_t i = 0; i < before.faces().size(); ++i) {
        const auto *face = after.resolve(inDomain(before.faceHandle(i), after));
        if (!face || !sameFace(before.faces()[i], *face))
            return false;
    }
    return true;
}
void validateEditGuards(const DocumentSession &source, const DocumentSession &preview,
                        const ai::EditRegion &region, const nlohmann::json &guards,
                        const ai::EditTrace &trace) {
    const auto &before = source.document, &after = preview.document;
    std::set<std::uint64_t> selectedFaces, selectedVertices, selectedMaterials, outsideVertices,
        outsideMaterials;
    for (auto h : region.faces)
        selectedFaces.insert(h.id);
    for (auto h : region.vertices)
        selectedVertices.insert(h.id);
    for (std::size_t i = 0; i < before.faces().size(); ++i) {
        const auto &face = before.faces()[i];
        if (selectedFaces.contains(before.faceHandle(i).id))
            selectedMaterials.insert(face.material.id);
        else {
            outsideMaterials.insert(face.material.id);
            for (auto h : face.vertices)
                outsideVertices.insert(h.id);
            if (guards.value("preserve_unselected", true)) {
                const auto *next = after.resolve(inDomain(before.faceHandle(i), after));
                if (!next || !sameFace(face, *next) || next->material.id != face.material.id ||
                    next->material.generation != face.material.generation)
                    reject("An unselected face changed");
            }
        }
    }
    std::map<std::uint64_t, const mmd::PmxVertex *> afterVertices;
    for (std::size_t i = 0; i < after.model().vertices.size(); ++i)
        afterVertices[after.vertexHandle(i).id] = &after.model().vertices[i];
    const bool uv = guards.value("preserve_uv", true),
               weights = guards.value("preserve_weights", true);
    std::set<std::uint64_t> originalIds;
    for (std::size_t i = 0; i < before.model().vertices.size(); ++i) {
        const auto h = before.vertexHandle(i);
        originalIds.insert(h.id);
        const auto &a = before.model().vertices[i];
        const auto *b = after.resolve(inDomain(h, after));
        if (guards.value("preserve_unselected", true) &&
            (!selectedVertices.contains(h.id) || outsideVertices.contains(h.id)) && (!b || a != *b))
            reject("An unselected or shared boundary vertex changed");
        if (uv && (!b || a.uv != b->uv || a.additionalUv != b->additionalUv))
            reject("Existing UV changed");
        if (weights && (!b || a.weightType != b->weightType || a.bones != b->bones ||
                        a.weights != b->weights || a.sdefC != b->sdefC || a.sdefR0 != b->sdefR0 ||
                        a.sdefR1 != b->sdefR1))
            reject("Existing weights changed");
    }
    if (guards.value("preserve_unselected", true))
        for (std::size_t i = 0; i < before.model().materials.size(); ++i) {
            const auto h = before.materialHandle(i);
            if (selectedMaterials.contains(h.id) && !outsideMaterials.contains(h.id))
                continue;
            const auto *m = after.resolve(inDomain(h, after));
            if (!m)
                reject("An unselected material was removed");
            auto value = *m;
            // Moving selected faces to a clone changes only the original range
            // length; the remaining faces must retain their appearance.
            if (selectedMaterials.contains(h.id))
                value.indexCount = before.model().materials[i].indexCount;
            if (value != before.model().materials[i])
                reject("An unselected material changed");
        }
    if (uv || weights)
        for (const auto &[id, b] : afterVertices) {
            if (originalIds.contains(id))
                continue;
            if (!trace.vertexSources.contains(id))
                reject("New vertex has no UV/weight interpolation provenance");
            mmd::PmxVertex expected;
            expected.uv = {};
            expected.additionalUv = {};
            Influences influence;
            const auto &parents = trace.vertexSources.at(id);
            float total = 0;
            for (const auto &[parent, factor] : parents) {
                if (!afterVertices.contains(parent) || !std::isfinite(factor) || factor < 0)
                    reject("Invalid vertex interpolation provenance");
                const auto &a = *afterVertices.at(parent);
                total += factor;
                for (std::size_t axis = 0; axis < 2; ++axis)
                    expected.uv[axis] += a.uv[axis] * factor;
                for (std::size_t channel = 0; channel < 4; ++channel)
                    for (std::size_t axis = 0; axis < 4; ++axis)
                        expected.additionalUv[channel][axis] +=
                            a.additionalUv[channel][axis] * factor;
                for (auto [bone, weight] : influences(a))
                    influence[bone] += weight * factor;
            }
            if (!near(total, 1))
                reject("Interpolation weights do not sum to one");
            if (uv) {
                for (std::size_t axis = 0; axis < 2; ++axis)
                    if (!near(expected.uv[axis], b->uv[axis]))
                        reject("New UV is not interpolated");
                for (std::size_t c = 0; c < 4; ++c)
                    for (std::size_t axis = 0; axis < 4; ++axis)
                        if (!near(expected.additionalUv[c][axis], b->additionalUv[c][axis]))
                            reject("New additional UV is not interpolated");
            }
            if (weights) {
                if (parents.size() == 1 && near(parents[0].second, 1)) {
                    const auto &a = *afterVertices.at(parents[0].first);
                    if (a.weightType != b->weightType || a.bones != b->bones ||
                        a.weights != b->weights || a.sdefC != b->sdefC || a.sdefR0 != b->sdefR0 ||
                        a.sdefR1 != b->sdefR1)
                        reject("Split vertex weights changed");
                } else {
                    std::vector<std::pair<std::int32_t, float>> sorted(influence.begin(),
                                                                       influence.end());
                    std::sort(sorted.begin(), sorted.end(),
                              [](auto a, auto b) { return a.second > b.second; });
                    if (sorted.size() > 4)
                        sorted.resize(4);
                    float sum = 0;
                    for (auto [bone, w] : sorted) {
                        (void)bone;
                        sum += w;
                    }
                    influence.clear();
                    for (auto [bone, w] : sorted)
                        if (sum > 0)
                            influence[bone] = w / sum;
                    const auto actual = influences(*b);
                    if (influence.size() != actual.size())
                        reject("Interpolated bone influences changed");
                    for (auto [bone, w] : influence)
                        if (!actual.contains(bone) || !near(w, actual.at(bone)))
                            reject("New weights are not interpolated");
                    if (b->weightType != mmd::PmxWeightType::bdef4 || b->sdefC != mmd::Float3{} ||
                        b->sdefR0 != mmd::Float3{} || b->sdefR1 != mmd::Float3{})
                        reject("Invalid interpolated skinning mode");
                }
            }
        }
    if (guards.value("preserve_morphs", true))
        morphs(before, after, trace);
}
} // namespace pmxer::editing
