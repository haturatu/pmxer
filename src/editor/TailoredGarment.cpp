#include "TailoredGarment.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>

namespace pmxer::ai {
namespace {
using V = mmd::Float3;
constexpr float pi = std::numbers::pi_v<float>;
V add(V a, V b) {
    for (std::size_t i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
V sub(V a, V b) {
    for (std::size_t i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
V mul(V a, float n) {
    for (auto &v : a)
        v *= n;
    return a;
}
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V unit(V a) {
    const auto n = std::sqrt(dot(a, a));
    return n > 1e-8F ? mul(a, 1 / n) : V{0, 1, 0};
}
V mix(V a, V b, float t) { return add(mul(a, 1 - t), mul(b, t)); }
float mix(float a, float b, float t) { return a + (b - a) * t; }
std::string normalizedName(std::string name) {
    std::erase_if(name,
                  [](unsigned char c) { return c == ' ' || c == '_' || c == '-' || c == '.'; });
    for (auto &c : name)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
std::optional<V> bone(const DocumentSession &s, std::initializer_list<const char *> names) {
    for (const auto &b : s.document.model().bones)
        for (const auto *name : names)
            if (normalizedName(b.name) == normalizedName(name) ||
                normalizedName(b.englishName) == normalizedName(name))
                return b.position;
    return {};
}
float quantile(std::vector<float> values, float fraction, float fallback) {
    if (values.empty())
        return fallback;
    const auto i = static_cast<std::size_t>(fraction * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(i), values.end());
    return values[i];
}
struct Fit {
    float x{}, z{}, hem{}, waist{}, shoulder{}, neck{}, halfWidth{}, depth{}, neckWidth{};
    V leftShoulder{}, rightShoulder{}, leftElbow{}, rightElbow{}, leftWrist{}, rightWrist{};
    const DocumentSession &session;
    const EditRegion &region;
    std::array<std::pair<float, float>, 5> profile{};
    // Cross-sections of the selected garment, restricted to the torso rather
    // than its arm span. This preserves a fitted silhouette on A/T-pose models.
    std::pair<float, float> measureSection(float y) const {
        std::vector<float> xs, zs;
        const auto band = std::max(0.04F, (neck - waist) * 0.12F);
        for (auto h : region.vertices) {
            const auto p = session.document.resolve(h)->position;
            if (std::abs(p[1] - y) > band || std::abs(p[0] - x) > halfWidth * 1.3F ||
                std::abs(p[2] - z) > depth * 1.6F)
                continue;
            xs.push_back(std::abs(p[0] - x));
            zs.push_back(std::abs(p[2] - z));
        }
        const auto w = quantile(xs, 0.94F, halfWidth * 0.8F);
        const auto d = quantile(zs, 0.95F, depth * 0.85F);
        return {std::clamp(w, halfWidth * 0.48F, halfWidth * 1.12F) * 1.035F,
                std::clamp(d, depth * 0.55F, depth * 1.3F) * 1.045F};
    }
    std::pair<float, float> section(float y) const {
        const auto t = std::clamp((y - waist) / std::max(0.01F, shoulder - waist), 0.0F, 1.0F) * 4;
        const auto i = std::min<std::size_t>(3, static_cast<std::size_t>(t));
        auto blend = t - static_cast<float>(i);
        blend = blend * blend * (3 - 2 * blend);
        return {mix(profile[i].first, profile[i + 1].first, blend),
                mix(profile[i].second, profile[i + 1].second, blend)};
    }
    std::pair<float, float> bodySection(float y) const {
        auto [w, d] = section(std::min(y, shoulder));
        if (y > shoulder) {
            const auto t =
                std::clamp((y - shoulder) / std::max(0.01F, neck - shoulder), 0.0F, 1.0F);
            w = mix(w, neckWidth, t);
            d = mix(d, neckWidth * 0.8F, t);
        }
        return {w, d};
    }
    float front(float y, float px = 0) const {
        const auto [w, d] = bodySection(y);
        return z - d * std::sqrt(std::max(0.05F, 1 - (px - x) * (px - x) / (w * w)));
    }
};
Fit measure(const DocumentSession &s, const EditRegion &r, const GarmentDesign &design) {
    const auto height = std::max(0.1F, r.maximum[1] - r.minimum[1]);
    const auto width = std::max(0.1F, r.maximum[0] - r.minimum[0]);
    Fit f{(r.minimum[0] + r.maximum[0]) * 0.5F,
          (r.minimum[2] + r.maximum[2]) * 0.5F,
          r.minimum[1],
          r.minimum[1] + height * 0.5F,
          r.maximum[1] - height * 0.13F,
          r.maximum[1],
          width * 0.23F,
          std::max(0.05F, (r.maximum[2] - r.minimum[2]) * 0.45F),
          0,
          {},
          {},
          {},
          {},
          {},
          {},
          s,
          r};
    const auto neck = bone(s, {"首", "neck", "J_Bip_C_Neck"});
    const auto left = bone(s, {"左腕", "arm_l", "leftarm", "LeftUpperArm", "J_Bip_L_UpperArm"});
    const auto right = bone(s, {"右腕", "arm_r", "rightarm", "RightUpperArm", "J_Bip_R_UpperArm"});
    if (neck && (*neck)[1] > r.minimum[1] + height * 0.5F &&
        (*neck)[1] < r.maximum[1] + height * 0.12F) {
        f.x = (*neck)[0];
        f.z = (*neck)[2];
        f.neck = (*neck)[1];
    }
    if (left && right && (*left)[1] > r.minimum[1] && (*right)[1] > r.minimum[1] &&
        (*left)[1] < f.neck + height * 0.1F && (*right)[1] < f.neck + height * 0.1F &&
        std::abs((*left)[0] - (*right)[0]) > height * 0.08F) {
        f.halfWidth = std::abs((*left)[0] - (*right)[0]) * 0.58F;
        f.shoulder = std::min(f.neck - height * 0.035F, ((*left)[1] + (*right)[1]) * 0.5F);
        f.leftShoulder = *left;
        f.rightShoulder = *right;
    } else {
        f.leftShoulder = {f.x + f.halfWidth * 0.9F, f.shoulder, f.z};
        f.rightShoulder = {f.x - f.halfWidth * 0.9F, f.shoulder, f.z};
    }
    if (const auto waist = bone(s, {"上半身", "upperbody", "spine", "J_Bip_C_Spine"});
        waist && (*waist)[1] > r.minimum[1] + height * 0.22F &&
        (*waist)[1] < f.shoulder - height * 0.12F)
        f.waist = (*waist)[1];
    f.waist = std::clamp(f.waist, r.minimum[1] + height * 0.25F, f.neck - height * 0.2F);
    f.neckWidth = f.halfWidth * 0.32F;
    // The central cross-section excludes long sleeves, side bows and appendages.
    std::vector<float> depths;
    for (auto h : r.vertices) {
        const auto p = s.document.resolve(h)->position;
        if (p[1] > f.waist && p[1] < f.shoulder && std::abs(p[0] - f.x) < f.halfWidth)
            depths.push_back(std::abs(p[2] - f.z));
    }
    f.depth = std::clamp(quantile(depths, 0.92F, f.depth), f.halfWidth * 0.35F, f.halfWidth * 0.9F);
    std::vector<float> hems;
    for (auto h : r.vertices) {
        const auto p = s.document.resolve(h)->position;
        if (p[1] < f.waist && std::abs(p[0] - f.x) < f.halfWidth * 2.6F)
            hems.push_back(p[1]);
    }
    f.hem = f.waist - std::max(height * 0.25F, f.waist - quantile(hems, 0.01F, r.minimum[1])) *
                          design.skirtLength;
    f.leftElbow =
        bone(s, {"左ひじ", "左肘", "elbow_l", "leftelbow", "LeftLowerArm", "J_Bip_L_LowerArm"})
            .value_or(add(f.leftShoulder, {f.halfWidth * 1.2F, -height * 0.16F, 0}));
    f.rightElbow =
        bone(s, {"右ひじ", "右肘", "elbow_r", "rightelbow", "RightLowerArm", "J_Bip_R_LowerArm"})
            .value_or(add(f.rightShoulder, {-f.halfWidth * 1.2F, -height * 0.16F, 0}));
    f.leftWrist = bone(s, {"左手首", "wrist_l", "leftwrist", "LeftHand", "J_Bip_L_Hand"})
                      .value_or(add(f.leftElbow, mul(sub(f.leftElbow, f.leftShoulder), 0.85F)));
    f.rightWrist = bone(s, {"右手首", "wrist_r", "rightwrist", "RightHand", "J_Bip_R_Hand"})
                       .value_or(add(f.rightElbow, mul(sub(f.rightElbow, f.rightShoulder), 0.85F)));
    for (std::size_t i = 0; i < f.profile.size(); ++i)
        f.profile[i] = f.measureSection(mix(f.waist, f.shoulder, static_cast<float>(i) / 4));
    return f;
}

// Nearest triangle interpolation, not nearest-vertex snapping: adjacent
// generated vertices receive continuous skin weights across the source surface.
struct Surface {
    struct Triangle {
        std::array<const mmd::PmxVertex *, 3> v;
        V lo, hi, c;
    };
    struct Node {
        V lo, hi;
        std::size_t begin{}, end{}, left{}, right{};
    };
    std::vector<Triangle> triangles;
    std::vector<std::size_t> indices;
    std::vector<Node> nodes;
    explicit Surface(const DocumentSession &s, const EditRegion &r) {
        for (auto h : r.faces) {
            const auto &face = *s.document.resolve(h);
            Triangle t{};
            for (std::size_t i = 0; i < 3; ++i)
                t.v[i] = s.document.resolve(face.vertices[i]);
            t.lo = t.hi = t.v[0]->position;
            for (auto v : t.v)
                for (std::size_t i = 0; i < 3; ++i) {
                    t.lo[i] = std::min(t.lo[i], v->position[i]);
                    t.hi[i] = std::max(t.hi[i], v->position[i]);
                    t.c[i] += v->position[i] / 3;
                }
            triangles.push_back(t);
        }
        indices.resize(triangles.size());
        std::iota(indices.begin(), indices.end(), std::size_t{0});
        nodes.reserve(triangles.size() * 2);
        build(0, indices.size());
    }
    std::size_t build(std::size_t begin, std::size_t end) {
        const auto id = nodes.size();
        nodes.push_back(
            {triangles[indices[begin]].lo, triangles[indices[begin]].hi, begin, end, 0, 0});
        for (auto i = begin; i < end; ++i)
            for (std::size_t a = 0; a < 3; ++a) {
                nodes[id].lo[a] = std::min(nodes[id].lo[a], triangles[indices[i]].lo[a]);
                nodes[id].hi[a] = std::max(nodes[id].hi[a], triangles[indices[i]].hi[a]);
            }
        if (end - begin > 8) {
            const auto size = sub(nodes[id].hi, nodes[id].lo);
            const auto axis =
                static_cast<std::size_t>(std::max_element(size.begin(), size.end()) - size.begin());
            const auto mid = begin + (end - begin) / 2;
            std::nth_element(
                indices.begin() + static_cast<std::ptrdiff_t>(begin),
                indices.begin() + static_cast<std::ptrdiff_t>(mid),
                indices.begin() + static_cast<std::ptrdiff_t>(end),
                [&](auto a, auto b) { return triangles[a].c[axis] < triangles[b].c[axis]; });
            nodes[id].left = build(begin, mid);
            nodes[id].right = build(mid, end);
        }
        return id;
    }
    static V barycentric(V p, const Triangle &t) {
        const auto a = t.v[0]->position, b = t.v[1]->position, c = t.v[2]->position;
        const auto ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
        const auto d1 = dot(ab, ap), d2 = dot(ac, ap);
        if (d1 <= 0 && d2 <= 0)
            return {1, 0, 0};
        const auto bp = sub(p, b);
        const auto d3 = dot(ab, bp), d4 = dot(ac, bp);
        if (d3 >= 0 && d4 <= d3)
            return {0, 1, 0};
        const auto vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0 && d1 - d3 > 1e-12F) {
            const auto v = d1 / (d1 - d3);
            return {1 - v, v, 0};
        }
        const auto cp = sub(p, c);
        const auto d5 = dot(ab, cp), d6 = dot(ac, cp);
        if (d6 >= 0 && d5 <= d6)
            return {0, 0, 1};
        const auto vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0 && d2 - d6 > 1e-12F) {
            const auto w = d2 / (d2 - d6);
            return {1 - w, 0, w};
        }
        const auto va = d3 * d6 - d5 * d4;
        if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0 && d4 - d3 + d5 - d6 > 1e-12F) {
            const auto w = (d4 - d3) / (d4 - d3 + d5 - d6);
            return {0, 1 - w, w};
        }
        const auto sum = va + vb + vc;
        if (std::abs(sum) < 1e-12F)
            return {1, 0, 0};
        return {va / sum, vb / sum, vc / sum};
    }
    static float distance(V p, const Node &n) {
        float result = 0;
        for (std::size_t a = 0; a < 3; ++a) {
            const auto delta = std::max({n.lo[a] - p[a], 0.0F, p[a] - n.hi[a]});
            result += delta * delta;
        }
        return result;
    }
    void query(std::size_t id, V p, float &best, std::size_t &index, V &bary) const {
        const auto &node = nodes[id];
        if (distance(p, node) > best)
            return;
        if (node.left) {
            auto a = node.left, b = node.right;
            if (distance(p, nodes[a]) > distance(p, nodes[b]))
                std::swap(a, b);
            query(a, p, best, index, bary);
            query(b, p, best, index, bary);
            return;
        }
        for (auto i = node.begin; i < node.end; ++i) {
            const auto &t = triangles[indices[i]];
            const auto weights = barycentric(p, t);
            V q{};
            for (std::size_t v = 0; v < 3; ++v)
                q = add(q, mul(t.v[v]->position, weights[v]));
            const auto d = dot(sub(p, q), sub(p, q));
            if (d < best) {
                best = d;
                index = indices[i];
                bary = weights;
            }
        }
    }
    void bind(mmd::PmxVertex &v) const {
        auto best = std::numeric_limits<float>::max();
        std::size_t index = 0;
        V bary{};
        query(0, v.position, best, index, bary);
        std::map<std::int32_t, float> weights;
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t j = 0; j < 4; ++j) {
                const auto &source = *triangles[index].v[i];
                if (source.bones[j] >= 0 && source.weights[j] > 0)
                    weights[source.bones[j]] += bary[i] * source.weights[j];
            }
        std::vector<std::pair<std::int32_t, float>> sorted(weights.begin(), weights.end());
        std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a.second > b.second; });
        v.bones.fill(-1);
        v.weights.fill(0);
        v.weightType = mmd::PmxWeightType::bdef4;
        float total = 0;
        for (std::size_t i = 0; i < std::min<std::size_t>(4, sorted.size()); ++i) {
            v.bones[i] = sorted[i].first;
            v.weights[i] = std::max(0.0F, sorted[i].second);
            total += v.weights[i];
        }
        if (total > 1e-8F)
            for (auto &w : v.weights)
                w /= total;
        else
            v.weights[0] = 1;
    }
};

struct Builder {
    GarmentGeometry mesh;
    std::size_t vertex(V p, float u = 0, float v = 0) {
        mmd::PmxVertex value;
        value.position = p;
        value.uv = {u, v};
        mesh.vertices.push_back(value);
        return mesh.vertices.size() - 1;
    }
    void triangle(std::size_t a, std::size_t b, std::size_t c, std::size_t material, V expected) {
        auto normal = cross(sub(mesh.vertices[b].position, mesh.vertices[a].position),
                            sub(mesh.vertices[c].position, mesh.vertices[a].position));
        if (dot(normal, normal) < 1e-14F)
            return;
        if (dot(normal, expected) < 0)
            std::swap(b, c);
        mesh.triangles.push_back({a, b, c, material});
    }
    void quad(std::size_t a, std::size_t b, std::size_t c, std::size_t d, std::size_t material,
              V expected) {
        triangle(a, b, c, material, expected);
        triangle(a, c, d, material, expected);
    }
    std::size_t material(const std::string &name, mmd::Float4 color) {
        mmd::PmxMaterial value;
        value.name = name;
        value.englishName = name;
        value.diffuse = color;
        value.drawFlags = 1;
        for (std::size_t i = 0; i < 3; ++i)
            value.ambient[i] = color[i] * 0.45F;
        value.specular = {0.12F, 0.12F, 0.12F};
        value.shininess = 12;
        mesh.materials.push_back(value);
        return mesh.materials.size() - 1;
    }
    template <class Fn>
    void surface(std::size_t rows, std::size_t columns, bool closed, std::size_t material,
                 Fn sample, V expected = {}) {
        const auto start = mesh.vertices.size();
        for (std::size_t row = 0; row <= rows; ++row)
            for (std::size_t column = 0; column < (closed ? columns : columns + 1); ++column)
                vertex(sample(static_cast<float>(row) / static_cast<float>(rows),
                              static_cast<float>(column) / static_cast<float>(columns)),
                       static_cast<float>(column) / static_cast<float>(columns),
                       static_cast<float>(row) / static_cast<float>(rows));
        const auto stride = closed ? columns : columns + 1;
        for (std::size_t row = 0; row < rows; ++row)
            for (std::size_t column = 0; column < columns; ++column) {
                const auto next = closed ? (column + 1) % columns : column + 1;
                if (dot(expected, expected) < 1e-12F)
                    quad(start + row * stride + column, start + row * stride + next,
                         start + (row + 1) * stride + next, start + (row + 1) * stride + column,
                         material, expected);
                else
                    quad(start + row * stride + column, start + (row + 1) * stride + column,
                         start + (row + 1) * stride + next, start + row * stride + next, material,
                         expected);
            }
    }
};
V radial(const Fit &f, float angle, float x, float z, float y) {
    return {f.x + std::cos(angle) * x, y, f.z + std::sin(angle) * z};
}
void bow(Builder &b, V p, float width, float height, std::size_t slot) {
    for (float side : {-1.0F, 1.0F}) {
        b.surface(4, 32, false, slot,
                  [&](float v, float u) {
                      const auto angle = 2 * pi * u;
                      const auto center =
                          V{side * width * (1 - std::cos(angle)) * 0.5F,
                            height * std::sin(angle) * 0.5F, -width * 0.08F * std::sin(pi * u)};
                      const auto tangent =
                          unit(V{side * width * std::sin(angle), height * std::cos(angle), 0});
                      return add(p, add(center, mul(V{-tangent[1], tangent[0], 0},
                                                    (v - 0.5F) * height * 0.46F)));
                  },
                  {0, 0, -1});
        b.surface(8, 3, false, slot,
                  [&](float t, float u) {
                      return add(
                          p,
                          {side * width * (0.10F + t * 0.16F) + (u - 0.5F) * width * 0.18F,
                           -height * (0.15F + t * 1.25F) + height * 0.12F * std::abs(2 * u - 1) * t,
                           -width * 0.02F - width * 0.06F * std::sin(t * pi)});
                  },
                  {0, 0, -1});
    }
    b.surface(4, 12, true, slot, [&](float t, float u) {
        return add(p, {width * 0.10F * std::cos(u * 2 * pi), (0.5F - t) * height * 0.48F,
                       width * 0.05F * std::sin(u * 2 * pi) - width * 0.025F});
    });
}
void trim(Builder &b, const Fit &f, float y, float rx, float rz, float height, float waves,
          std::size_t segments, std::size_t slot, bool lace) {
    b.surface(5, segments, true, slot, [&](float t, float u) {
        const auto a = u * 2 * pi;
        const auto r = 1 + t * 0.055F + 0.025F * std::sin(a * waves) * std::sin(t * pi * 0.5F);
        return radial(f, a, rx * r, rz * r,
                      y - height * t - height * 0.12F * std::cos(a * waves) * t);
    });
    if (!lace)
        return;
    // Openwork scalloped lace: paired rails and small bridges, with real holes
    // between them, rather than a painted white stripe.
    const auto holes = segments / 2;
    for (std::size_t i = 0; i < holes; ++i) {
        const auto center = 2 * pi * static_cast<float>(i) / static_cast<float>(holes);
        b.surface(2, 8, false, slot, [&](float t, float u) {
            const auto a = center + (u - 0.5F) * 2 * pi / static_cast<float>(holes);
            const auto scallop = std::sin(u * pi);
            return radial(f, a, rx * 1.06F * (1 + 0.02F * scallop),
                          rz * 1.06F * (1 + 0.02F * scallop),
                          y - height - height * (0.05F + 0.42F * scallop + 0.075F * t));
        });
    }
}
void buildDress(Builder &b, const Fit &f, const Operation &op) {
    const auto gothic = op.garment.style == GarmentStyle::gothicLolita;
    const auto prefix = std::string(gothic ? "AI Gothic " : "AI Tailored ");
    const auto bodice = b.material(prefix + "Bodice", op.color),
               sleeves = b.material(prefix + "Sleeves", op.color),
               skirt = b.material(prefix + "Skirt", op.color),
               ruffles = b.material(prefix + "Ruffles", op.garment.trimColor),
               collar = b.material(prefix + "Collar", op.garment.trimColor),
               ribbons = b.material(prefix + "Ribbons", op.garment.accentColor),
               cuffs = b.material(prefix + "Cuffs", op.garment.trimColor);
    const auto n = static_cast<std::size_t>(std::clamp(op.detail * 3, 48, 144));
    const auto torso = f.neck - f.waist, skirtHeight = f.waist - f.hem;
    // Fit the torso first, then close its shoulder area around a distinct neck
    // opening. Sleeve openings are cut out at the upper sides of the bodice.
    constexpr std::size_t rows = 24;
    const auto base = b.mesh.vertices.size();
    for (std::size_t row = 0; row <= rows; ++row) {
        const auto y = mix(f.waist, f.neck, static_cast<float>(row) / static_cast<float>(rows));
        const auto [w, d] = f.bodySection(y);
        for (std::size_t i = 0; i < n; ++i)
            b.vertex(radial(f, static_cast<float>(i) * 2 * pi / static_cast<float>(n), w, d, y),
                     static_cast<float>(i) / static_cast<float>(n),
                     static_cast<float>(row) / static_cast<float>(rows));
    }
    for (std::size_t row = 0; row < rows; ++row)
        for (std::size_t i = 0; i < n; ++i) {
            const auto angle = (static_cast<float>(i) + 0.5F) * 2 * pi / static_cast<float>(n);
            const auto y =
                mix(f.waist, f.neck, (static_cast<float>(row) + 0.5F) / static_cast<float>(rows));
            const auto dy = (y - (f.shoulder - torso * 0.05F)) / (f.halfWidth * 0.38F);
            if (std::abs(std::sin(angle)) < 0.52F &&
                dy * dy + std::sin(angle) * std::sin(angle) / 0.27F < 1)
                continue;
            const auto next = (i + 1) % n;
            b.quad(base + row * n + i, base + (row + 1) * n + i, base + (row + 1) * n + next,
                   base + row * n + next, bodice, {std::cos(angle), 0, std::sin(angle)});
        }
    // Layered bell silhouette keeps fullness away from the legs instead of
    // projecting the entire skirt onto the body surface.
    const auto [waistX, waistZ] = f.section(f.waist);
    const auto radii = [&](float t) {
        const auto expansion = std::pow(std::clamp(t, 0.0F, 1.0F), 0.65F) * op.garment.fullness;
        return std::pair{waistX + f.halfWidth * 1.15F * expansion,
                         waistZ + f.halfWidth * 0.95F * expansion};
    };
    b.surface(30, n, true, skirt, [&](float t, float u) {
        auto [x, z] = radii(t);
        const auto a = u * 2 * pi;
        const auto fold = 1 + (gothic ? 0.018F : 0.009F) * std::sin(a * 24) * t;
        return radial(f, a, x * fold, z * fold, f.waist - t * skirtHeight);
    });
    const int tiers = gothic ? op.garment.tiers : 1;
    for (int layer = 0; layer < tiers; ++layer) {
        const auto end = static_cast<float>(layer + 1) / static_cast<float>(tiers);
        const auto start = std::max(0.0F, end - 0.35F);
        if (gothic)
            b.surface(8, n, true, skirt, [&](float t, float u) {
                const auto level = mix(start, end, t);
                auto [x, z] = radii(level);
                const auto a = u * 2 * pi;
                const auto wave = 1 + 0.018F * std::sin(a * 24) * t;
                return radial(f, a, x * (1.035F + 0.035F * t) * wave,
                              z * (1.035F + 0.035F * t) * wave, f.waist - level * skirtHeight);
            });
        auto [x, z] = radii(end);
        trim(b, f, f.waist - end * skirtHeight, x * 1.07F, z * 1.07F,
             skirtHeight * (gothic ? 0.052F : 0.025F), 24, n, ruffles, gothic && op.garment.lace);
    }
    // Project the rounded collar onto the newly constructed bodice, not the
    // source garment's bounding box. Decorations must remain in front of the cloth.
    for (float side : {-1.0F, 1.0F})
        b.surface(6, n / 4, false, collar,
                  [&](float t, float u) {
                      const auto angle =
                          (side < 0 ? pi + 0.04F : pi * 1.5F + 0.04F) + u * (pi * 0.5F - 0.08F);
                      const auto lobe = std::sin(u * pi);
                      const auto x =
                          f.x + std::cos(angle) * mix(f.neckWidth, f.halfWidth * 0.78F, t);
                      const auto y = f.neck - torso * mix(0.018F, 0.21F + 0.10F * lobe, t);
                      return V{x, y, f.front(y, x) - f.depth * (0.07F + 0.012F * std::sin(t * pi))};
                  },
                  {0, 0, -1});
    // Puff sleeves and cuffs align with the original arm bones, including A-pose.
    if (op.garment.sleeveLength > 0)
        for (const auto &arm : {std::array<V, 3>{f.leftShoulder, f.leftElbow, f.leftWrist},
                                std::array<V, 3>{f.rightShoulder, f.rightElbow, f.rightWrist}}) {
            const auto direction = unit(sub(arm[1], arm[0]));
            const auto radius = f.halfWidth * 0.34F;
            const auto point = [&](float length) {
                if (length <= 1)
                    return sub(mix(arm[0], arm[1], length),
                               mul(direction, f.halfWidth * 0.17F * (1 - length)));
                return mix(arm[1], arm[2], length - 1);
            };
            const auto axisAt = [&](float length) {
                const auto blend = std::clamp((length - 0.8F) / 0.4F, 0.0F, 1.0F);
                return unit(mix(direction, unit(sub(arm[2], arm[1])), blend));
            };
            b.surface(28, n / 2, true, sleeves, [&](float t, float s) {
                const auto length = t * op.garment.sleeveLength;
                const auto axis = axisAt(length), u = unit(cross(axis, {0, 0, 1})),
                           v = unit(cross(axis, u));
                const auto puff =
                    length < 0.85F
                        ? 0.75F + (gothic ? 0.8F : 0.25F) * std::sin(length * pi / 0.85F) -
                              0.15F * length
                        : mix(0.62F, 0.46F, std::clamp((length - 0.85F) / 1.15F, 0.0F, 1.0F));
                const auto angle = s * 2 * pi;
                return add(point(length), add(mul(u, radius * puff * std::cos(angle)),
                                              mul(v, radius * puff * std::sin(angle))));
            });
            const auto end = point(op.garment.sleeveLength), axis = axisAt(op.garment.sleeveLength),
                       u = unit(cross(axis, {0, 0, 1})), v = unit(cross(axis, u));
            const auto endRadius = radius * (op.garment.sleeveLength >= 1 ? 0.49F : 0.70F);
            b.surface(5, n / 2, true, cuffs, [&](float t, float s) {
                const auto center = add(end, mul(axis, (t - 0.15F) * radius * 0.55F));
                const auto angle = s * 2 * pi;
                const auto r = endRadius * (1 + 0.12F * std::sin(angle * 12) * t);
                return add(center, add(mul(u, r * std::cos(angle)), mul(v, r * std::sin(angle))));
            });
        }
    if (gothic && op.garment.bowSize > 0) {
        const auto width = f.halfWidth * 0.50F * op.garment.bowSize;
        const auto chestY = f.neck - torso * 0.25F;
        bow(b, {f.x, chestY, f.front(chestY) - f.depth * 0.08F}, width, width * 0.52F, ribbons);
        bow(b, {f.x, f.waist + torso * 0.05F, f.front(f.waist + torso * 0.05F) - f.depth * 0.12F},
            width * 1.15F, width * 0.52F, ribbons);
        // Contrasting corset lacing, on the torso surface rather than floating
        // at the bounding box's frontmost decorative vertex.
        const auto slot = b.material(prefix + "Lacing", op.garment.trimColor);
        for (int i = 0; i < 5; ++i)
            for (float side : {-1.0F, 1.0F})
                b.surface(1, 8, false, slot,
                          [&](float t, float s) {
                              const auto x = f.x + side * f.halfWidth * 0.15F * (1 - 2 * s);
                              const auto y =
                                  f.waist +
                                  torso * (0.15F + static_cast<float>(i) * 0.085F + s * 0.085F) +
                                  (t - 0.5F) * torso * 0.014F;
                              return V{x, y, f.front(y, x) - f.depth * 0.035F};
                          },
                          {0, 0, -1});
    }
}
V rotate(V p, V degrees) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
        const auto angle = degrees[axis] * pi / 180;
        const auto first = p[a], second = p[b];
        p[a] = std::cos(angle) * first - std::sin(angle) * second;
        p[b] = std::sin(angle) * first + std::cos(angle) * second;
    }
    return p;
}
} // namespace

GarmentGeometry makeTailoredDress(const DocumentSession &session, const EditRegion &region,
                                  const Operation &op) {
    if (op.type != OperationType::generate || op.shape != Shape::dress || op.detail < 8 ||
        op.detail > 64)
        throw std::runtime_error("Invalid local garment operation");
    if (region.domain != session.document.domain() || region.documentId != session.recoveryId ||
        region.revision != session.revision)
        throw std::runtime_error("Stale local garment selection");
    for (auto h : region.faces)
        if (!session.document.resolve(h))
            throw std::runtime_error("Stale local garment selection");
    for (auto h : region.vertices)
        if (!session.document.resolve(h))
            throw std::runtime_error("Stale local garment selection");
    for (std::size_t i = 0; i < 3; ++i)
        if (!std::isfinite(op.scale[i]) || op.scale[i] < 0.05F || op.scale[i] > 20 ||
            !std::isfinite(op.pivot[i]) || op.pivot[i] < 0 || op.pivot[i] > 1 ||
            !std::isfinite(op.rotation[i]) || std::abs(op.rotation[i]) > 180 ||
            !std::isfinite(op.translation[i]) || std::abs(op.translation[i]) > 100)
            throw std::runtime_error("Invalid local garment transform");
    if (region.maximum[1] - region.minimum[1] < 1e-4F)
        throw std::runtime_error("Select a clothing region with vertical extent");
    if (region.faces.empty())
        throw std::runtime_error("Select clothing first");
    const auto &d = op.garment;
    if (d.style < GarmentStyle::plain || d.style > GarmentStyle::gothicLolita || d.tiers < 1 ||
        d.tiers > 4 || !std::isfinite(d.skirtLength) || d.skirtLength < 0.5F ||
        d.skirtLength > 1.5F || !std::isfinite(d.sleeveLength) || d.sleeveLength < 0 ||
        d.sleeveLength > 2.1F || !std::isfinite(d.fullness) || d.fullness < 0.6F ||
        d.fullness > 1.6F || !std::isfinite(d.bowSize) || d.bowSize < 0 || d.bowSize > 1.5F)
        throw std::runtime_error("Invalid local garment parameters");
    for (const auto &color : {op.color, d.trimColor, d.accentColor})
        for (auto c : color)
            if (!std::isfinite(c) || c < 0 || c > 1)
                throw std::runtime_error("Invalid garment color");
    Builder builder;
    const auto fit = measure(session, region, d);
    buildDress(builder, fit, op);
    Surface source(session, region);
    V pivot{};
    for (std::size_t i = 0; i < 3; ++i)
        pivot[i] = region.minimum[i] + (region.maximum[i] - region.minimum[i]) * op.pivot[i];
    for (auto &v : builder.mesh.vertices) {
        source.bind(v);
        auto p = sub(v.position, pivot);
        for (std::size_t i = 0; i < 3; ++i)
            p[i] *= op.scale[i];
        v.position = add(add(rotate(p, op.rotation), pivot), op.translation);
        v.normal = {};
    }
    for (const auto &face : builder.mesh.triangles) {
        const auto normal = cross(
            sub(builder.mesh.vertices[face.b].position, builder.mesh.vertices[face.a].position),
            sub(builder.mesh.vertices[face.c].position, builder.mesh.vertices[face.a].position));
        for (auto index : {face.a, face.b, face.c})
            builder.mesh.vertices[index].normal = add(builder.mesh.vertices[index].normal, normal);
    }
    for (auto &v : builder.mesh.vertices)
        v.normal = unit(v.normal);
    return std::move(builder.mesh);
}

EditTarget suggestOutfitTarget(const DocumentSession &session, const EditRegion &region) {
    std::set<mmd::MaterialHandle> touched;
    for (auto h : region.faces)
        touched.insert(session.document.resolve(h)->material);
    EditTarget result;
    result.completeMaterials = region.allowMaterialExpansion;
    for (std::size_t i = 0; i < session.document.model().materials.size(); ++i) {
        if (!touched.contains(session.document.materialHandle(i)))
            continue;
        const auto &m = session.document.model().materials[i];
        const auto name = normalizedName(m.name + " " + m.englishName);
        bool excluded = false;
        for (const auto *word :
             {"hair",      "face",   "skin",   "eye",  "mouth", "shoe",  "boot", "sock",
              "underwear", "body",   "ボディ", "髪",   "顔",    "肌",    "目",   "口",
              "靴",        "ニーソ", "下着",   "头发", "皮肤",  "волос", "кож"})
            excluded |= name.find(word) != std::string::npos;
        bool clothing = m.name.starts_with("AI Gothic ") || m.name.starts_with("AI Tailored ");
        for (const auto *word :
             {"cloth", "dress", "skirt", "shirt", "coat", "jacket", "bodice", "sleeve", "服",
              "スカート", "袖", "衣", "裙", "одеж", "юбк", "плать"})
            clothing |= name.find(word) != std::string::npos;
        if (clothing && !excluded)
            result.materialIndices.push_back(i);
    }
    if (result.materialIndices.empty())
        throw std::runtime_error("Choose clothing materials for the local outfit preview");
    return result;
}
} // namespace pmxer::ai
