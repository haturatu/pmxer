#include "AiMeshOperations.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace pmxer::ai {
namespace {
using V = mmd::Float3;
using Handle = mmd::VertexHandle;
using Edge = std::pair<Handle, Handle>;
Edge edge(Handle a, Handle b) { return a < b ? Edge{a, b} : Edge{b, a}; }
V unit(V v) {
    const auto length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 1e-8F)
        return {0, 1, 0};
    for (auto &value : v)
        value /= length;
    return v;
}
void normals(mmd::PmxDocument &doc, mmd::PmxDocument::Transaction &tx,
             const std::vector<std::array<Handle, 3>> &triangles, const std::set<Handle> &fixed,
             const std::map<Handle, V> &positions) {
    const auto position = [&](Handle h) {
        return positions.contains(h) ? positions.at(h) : doc.resolve(h)->position;
    };
    std::map<Handle, V> sums;
    for (const auto &face : triangles) {
        const auto a = position(face[0]), b = position(face[1]), c = position(face[2]);
        V ab{}, ac{};
        for (std::size_t i = 0; i < 3; ++i) {
            ab[i] = b[i] - a[i];
            ac[i] = c[i] - a[i];
        }
        const V normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                       ab[0] * ac[1] - ab[1] * ac[0]};
        for (auto h : face)
            for (std::size_t i = 0; i < 3; ++i)
                sums[h][i] += normal[i];
    }
    for (const auto &[h, sum] : sums) {
        if (fixed.contains(h))
            continue;
        auto v = *doc.resolve(h);
        v.position = position(h);
        v.normal = unit(sum);
        if (!tx.setVertex(h, v))
            throw std::runtime_error("AI normal update failed");
    }
}
mmd::PmxVertex midpoint(const mmd::PmxVertex &a, const mmd::PmxVertex &b) {
    auto v = a;
    for (std::size_t i = 0; i < 3; ++i) {
        v.position[i] = (a.position[i] + b.position[i]) * 0.5F;
        v.normal[i] = a.normal[i] + b.normal[i];
    }
    v.normal = unit(v.normal);
    for (std::size_t i = 0; i < 2; ++i)
        v.uv[i] = (a.uv[i] + b.uv[i]) * 0.5F;
    for (std::size_t channel = 0; channel < 4; ++channel)
        for (std::size_t i = 0; i < 4; ++i)
            v.additionalUv[channel][i] =
                (a.additionalUv[channel][i] + b.additionalUv[channel][i]) * 0.5F;
    v.edgeScale = (a.edgeScale + b.edgeScale) * 0.5F;
    std::map<std::int32_t, float> weights;
    for (const auto *source : {&a, &b})
        for (std::size_t i = 0; i < 4; ++i)
            if (source->bones[i] >= 0 && source->weights[i] > 0)
                weights[source->bones[i]] += source->weights[i] * 0.5F;
    std::vector<std::pair<std::int32_t, float>> sorted(weights.begin(), weights.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto first, auto second) { return first.second > second.second; });
    v.bones.fill(-1);
    v.weights.fill(0);
    float total = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(4, sorted.size()); ++i) {
        v.bones[i] = sorted[i].first;
        v.weights[i] = sorted[i].second;
        total += sorted[i].second;
    }
    if (total > 0)
        for (auto &weight : v.weights)
            weight /= total;
    else
        v.weights[0] = 1;
    v.weightType = mmd::PmxWeightType::bdef4;
    v.sdefC = {};
    v.sdefR0 = {};
    v.sdefR1 = {};
    return v;
}
} // namespace
void editMesh(mmd::PmxDocument &doc, mmd::PmxDocument::Transaction &tx, const EditRegion &region,
              const Operation &op) {
    if (op.type == OperationType::subdivide) {
        if (region.faces.size() > 250000 ||
            doc.faces().size() + region.faces.size() * 3 > 2000000 ||
            doc.model().vertices.size() + region.faces.size() * 3 > 2000000)
            throw std::runtime_error("Mesh resolution limit reached; use fewer "
                                     "subdivisions or a smaller area");
        // Save morph data before appending offsets. Interpolate absent offsets as
        // zero.
        struct MorphData {
            mmd::MorphHandle handle;
            std::uint8_t type;
            std::map<Handle, mmd::PmxMorphOffset> offsets;
        };
        std::vector<MorphData> morphs;
        for (std::size_t i = 0; i < doc.model().morphs.size(); ++i) {
            const auto &morph = doc.model().morphs[i];
            if (morph.type != 1 && (morph.type < 3 || morph.type > 7))
                continue;
            MorphData data{doc.morphHandle(i), morph.type, {}};
            for (const auto &offset : morph.offsets)
                if (offset.index >= 0 &&
                    static_cast<std::size_t>(offset.index) < doc.model().vertices.size()) {
                    auto &sum =
                        data.offsets[doc.vertexHandle(static_cast<std::size_t>(offset.index))];
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        sum.vector3[axis] += offset.vector3[axis];
                    for (std::size_t axis = 0; axis < 4; ++axis)
                        sum.vector4[axis] += offset.vector4[axis];
                }
            morphs.push_back(std::move(data));
        }
        std::map<Edge, Handle> midpoints;
        const auto middle = [&](Handle a, Handle b) {
            const auto key = edge(a, b);
            if (midpoints.contains(key))
                return midpoints.at(key);
            const auto vertex = midpoint(*doc.resolve(a), *doc.resolve(b));
            const auto h = tx.addVertex(vertex);
            midpoints[key] = h;
            for (const auto &morph : morphs) {
                if (!morph.offsets.contains(a) && !morph.offsets.contains(b))
                    continue;
                mmd::PmxMorphOffset first{}, second{};
                if (morph.offsets.contains(a))
                    first = morph.offsets.at(a);
                if (morph.offsets.contains(b))
                    second = morph.offsets.at(b);
                bool ok = false;
                if (morph.type == 1) {
                    V value{};
                    for (std::size_t i = 0; i < 3; ++i)
                        value[i] = (first.vector3[i] + second.vector3[i]) * 0.5F;
                    ok = tx.addVertexMorphOffset(morph.handle, h, value);
                } else {
                    mmd::Float4 value{};
                    for (std::size_t i = 0; i < 4; ++i)
                        value[i] = (first.vector4[i] + second.vector4[i]) * 0.5F;
                    ok = tx.addUvMorphOffset(morph.handle, h, morph.type - 3U, value);
                }
                if (!ok)
                    throw std::runtime_error("AI subdivided morph update failed");
            }
            return h;
        };
        // Delete before appending: interleaving would shift the growing output
        // four times for every input triangle. The document still holds the
        // immutable input while the transaction builds the new face graph.
        for (auto it = region.faces.rbegin(); it != region.faces.rend(); ++it)
            if (!tx.eraseFace(*it))
                throw std::runtime_error("AI subdivision lost a face");
        for (auto h : region.faces) {
            const auto face = *doc.resolve(h);
            const auto a = face.vertices[0], b = face.vertices[1], c = face.vertices[2];
            const auto ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
            (void)tx.addFace(a, ab, ca, face.material);
            (void)tx.addFace(ab, b, bc, face.material);
            (void)tx.addFace(ca, bc, c, face.material);
            (void)tx.addFace(ab, bc, ca, face.material);
        }
        return;
    }
    std::map<Handle, std::set<Handle>> neighbors;
    std::map<Edge, unsigned> edgeCounts;
    std::vector<std::array<Handle, 3>> triangles;
    for (auto h : region.faces) {
        const auto face = *doc.resolve(h);
        triangles.push_back({face.vertices[0], face.vertices[1], face.vertices[2]});
        for (std::size_t i = 0; i < 3; ++i) {
            const auto a = face.vertices[i], b = face.vertices[(i + 1) % 3];
            neighbors[a].insert(b);
            neighbors[b].insert(a);
            ++edgeCounts[edge(a, b)];
        }
    }
    std::set<Handle> fixed;
    for (const auto &[pair, count] : edgeCounts)
        if (count != 2) {
            fixed.insert(pair.first);
            fixed.insert(pair.second);
        }
    const std::set<mmd::FaceHandle> selected(region.faces.begin(), region.faces.end());
    for (std::size_t i = 0; i < doc.faces().size(); ++i)
        if (!selected.contains(doc.faceHandle(i)))
            for (auto v : doc.faces()[i].vertices)
                fixed.insert(v);
    // Compute all positions from the same original frame to avoid order
    // dependence.
    std::map<Handle, V> positions;
    for (const auto &[h, adjacent] : neighbors) {
        if (fixed.contains(h))
            continue;
        const auto &v = *doc.resolve(h);
        auto p = v.position;
        if (op.type == OperationType::smooth) {
            V mean{};
            for (auto other : adjacent)
                for (std::size_t i = 0; i < 3; ++i)
                    mean[i] +=
                        doc.resolve(other)->position[i] / static_cast<float>(adjacent.size());
            for (std::size_t i = 0; i < 3; ++i)
                p[i] += (mean[i] - p[i]) * op.amount;
        } else {
            const auto normal = unit(v.normal);
            for (std::size_t i = 0; i < 3; ++i)
                p[i] += normal[i] * op.amount;
        }
        positions[h] = p;
    }
    for (const auto &[h, p] : positions) {
        auto v = *doc.resolve(h);
        v.position = p;
        if (!tx.setVertex(h, v))
            throw std::runtime_error("AI deformation lost a vertex");
    }
    normals(doc, tx, triangles, fixed, positions);
}
} // namespace pmxer::ai
