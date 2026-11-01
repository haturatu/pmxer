#include "ModelQuery.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace pmxer::editing {
using Json = nlohmann::json;
bool globMatch(std::string_view pattern, std::string_view value) {
    std::size_t p = 0, v = 0, star = std::string_view::npos, retry = 0;
    while (v < value.size()) {
        if (p < pattern.size() && pattern[p] == value[v]) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = v;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            v = ++retry;
        } else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}
std::vector<std::size_t> materialFaces(const mmd::PmxModel &m,
                                       const std::vector<std::size_t> &materials) {
    std::vector<std::size_t> result;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < m.materials.size(); ++i) {
        const auto end = begin + m.materials[i].indexCount;
        if (end > m.indices.size() || begin % 3 || end % 3)
            throw std::runtime_error("Invalid material face range");
        if (std::find(materials.begin(), materials.end(), i) != materials.end())
            for (auto j = begin / 3; j < end / 3; ++j)
                result.push_back(j);
        begin = end;
    }
    return result;
}
std::vector<std::size_t> findComponents(const mmd::PmxModel &m, const Query &q) {
    std::vector<std::size_t> result;
    auto matches = [&](const auto &v) {
        return globMatch(q.name, v.name) || globMatch(q.name, v.englishName);
    };
    if (q.kind == "materials") {
        for (std::size_t i = 0; i < m.materials.size(); ++i)
            if (matches(m.materials[i]))
                result.push_back(i);
    } else if (q.kind == "bones" || q.kind == "bone") {
        for (std::size_t i = 0; i < m.bones.size(); ++i)
            if (matches(m.bones[i]))
                result.push_back(i);
    } else if (q.kind == "morphs") {
        for (std::size_t i = 0; i < m.morphs.size(); ++i)
            if (matches(m.morphs[i]))
                result.push_back(i);
    } else if (q.kind == "vertices") {
        if (q.name != "*")
            throw std::runtime_error(
                "Vertices have no names; use material, bone or bounds filters");
        std::set<std::size_t> allowed;
        if (!q.material.empty()) {
            Query mq;
            mq.name = q.material;
            for (auto face : materialFaces(m, findComponents(m, mq)))
                for (std::size_t j = 0; j < 3; ++j)
                    allowed.insert(m.indices.at(face * 3 + j));
        }
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            const auto &v = m.vertices[i];
            if (!q.material.empty() && !allowed.contains(i))
                continue;
            bool inside = true, influenced = q.bone.empty();
            for (std::size_t a = 0; a < 3; ++a)
                inside &= (!q.minimum || v.position[a] >= (*q.minimum)[a]) &&
                          (!q.maximum || v.position[a] <= (*q.maximum)[a]);
            for (std::size_t a = 0; a < 4; ++a)
                if (v.bones[a] >= 0 && static_cast<std::size_t>(v.bones[a]) < m.bones.size() &&
                    v.weights[a] > 0 &&
                    (globMatch(q.bone, m.bones[static_cast<std::size_t>(v.bones[a])].name) ||
                     globMatch(q.bone, m.bones[static_cast<std::size_t>(v.bones[a])].englishName)))
                    influenced = true;
            if (inside && influenced)
                result.push_back(i);
        }
    } else
        throw std::runtime_error("Expected materials, bones, bone, morphs or vertices");
    if (q.kind != "vertices" && (!q.material.empty() || !q.bone.empty() || q.minimum || q.maximum))
        throw std::runtime_error("Material, bone and bounds filters require vertices");
    return result;
}
Json inspect(const mmd::PmxModel &m, const Query &q) {
    if (q.limit < 1 || q.limit > 1000)
        throw std::runtime_error("Query limit must be 1..1000");
    const auto indices = findComponents(m, q);
    Json items = Json::array();
    const auto begin = std::min(q.offset, indices.size());
    const auto end = begin + std::min(q.limit, indices.size() - begin);
    for (auto n = begin; n < end; ++n) {
        const auto i = indices[n];
        Json item{{"index", i}};
        if (q.kind == "materials") {
            const auto &v = m.materials[i];
            item.update({{"name", v.name},
                         {"english_name", v.englishName},
                         {"diffuse", v.diffuse},
                         {"texture_index", v.textureIndex},
                         {"faces", v.indexCount / 3}});
            std::set<std::size_t> vertices;
            std::set<std::int32_t> bones;
            for (auto f : materialFaces(m, {i}))
                for (std::size_t a = 0; a < 3; ++a)
                    vertices.insert(m.indices.at(f * 3 + a));
            for (auto vi : vertices) {
                const auto &vertex = m.vertices.at(vi);
                for (std::size_t a = 0; a < 4; ++a)
                    if (vertex.bones[a] >= 0 && vertex.weights[a] > 0)
                        bones.insert(vertex.bones[a]);
            }
            if (!vertices.empty()) {
                auto minimum = m.vertices.at(*vertices.begin()).position, maximum = minimum;
                for (auto vi : vertices)
                    for (std::size_t a = 0; a < 3; ++a) {
                        minimum[a] = std::min(minimum[a], m.vertices.at(vi).position[a]);
                        maximum[a] = std::max(maximum[a], m.vertices.at(vi).position[a]);
                    }
                item["bounds"] = {{"minimum", minimum}, {"maximum", maximum}};
            } else
                item["bounds"] = nullptr;
            item["vertices"] = vertices.size();
            if (q.includeBones)
                item["bone_indices"] = bones;
        } else if (q.kind == "bones" || q.kind == "bone") {
            const auto &v = m.bones[i];
            item.update({{"name", v.name},
                         {"english_name", v.englishName},
                         {"position", v.position},
                         {"parent", v.parent},
                         {"ik_target", v.ikTarget},
                         {"flags", v.flags}});
            std::size_t count = 0;
            float sum = 0;
            for (const auto &vertex : m.vertices) {
                bool influenced = false;
                for (std::size_t a = 0; a < 4; ++a)
                    if (vertex.bones[a] == static_cast<std::int32_t>(i) && vertex.weights[a] > 0) {
                        influenced = true;
                        sum += vertex.weights[a];
                    }
                if (influenced)
                    ++count;
            }
            if (q.includeWeights)
                item["influence"] = {{"vertices", count},
                                     {"weight_sum", sum},
                                     {"details_command",
                                      "query MODEL vertices --bone NAME --include-weights --json"}};
        } else if (q.kind == "morphs") {
            const auto &v = m.morphs[i];
            item.update({{"name", v.name},
                         {"english_name", v.englishName},
                         {"type", v.type},
                         {"panel", v.panel},
                         {"offsets", v.offsets.size()}});
        } else {
            const auto &v = m.vertices[i];
            item.update({{"position", v.position}, {"normal", v.normal}, {"uv", v.uv}});
            if (q.includeBones || q.includeWeights)
                item["bone_indices"] = v.bones;
            if (q.includeWeights)
                item["weights"] = v.weights;
        }
        items.push_back(std::move(item));
    }
    return {{"schema_version", 1},
            {"kind", q.kind},
            {"total", indices.size()},
            {"offset", q.offset},
            {"limit", q.limit},
            {"next_offset", end < indices.size() ? Json(end) : Json(nullptr)},
            {"items", items}};
}
} // namespace pmxer::editing
