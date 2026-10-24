#include "ViewportPicking.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/PreviewPoseQueries.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace pmxer {
namespace {

bool insideTriangle(ImVec2 point, ImVec2 a, ImVec2 b, ImVec2 c) {
    const auto sign = [](ImVec2 first, ImVec2 second, ImVec2 third) {
        return (first.x - third.x) * (second.y - third.y) -
               (second.x - third.x) * (first.y - third.y);
    };
    const auto first = sign(point, a, b);
    const auto second = sign(point, b, c);
    const auto third = sign(point, c, a);
    if (std::abs(sign(a, b, c)) < 1e-6F)
        return false;
    return !((first < 0.0F || second < 0.0F || third < 0.0F) &&
             (first > 0.0F || second > 0.0F || third > 0.0F));
}

bool cacheMatches(const ViewportPickCache &cache, const DocumentSession &session,
                  std::size_t vertexCount, const CameraState &camera, ImVec2 origin,
                  ImVec2 size) {
    return !cache.grid.empty() && cache.vertices.size() == vertexCount &&
           cache.revision == session.revision &&
           cache.frameRevision == session.preview.frameRevision &&
           cache.cameraYaw == camera.yaw && cache.cameraPitch == camera.pitch &&
           cache.cameraDistance == camera.distance &&
           cache.cameraTargetX == camera.target[0] &&
           cache.cameraTargetY == camera.target[1] &&
           cache.cameraTargetZ == camera.target[2] &&
           cache.orthographic == camera.orthographic && cache.originX == origin.x &&
           cache.originY == origin.y && cache.width == size.x && cache.height == size.y;
}

ViewportPickCache &ensurePickCache(const DocumentSession &session,
                                   const std::vector<mmd::PmxVertex> &vertices,
                                   const CameraState &camera, ImVec2 origin,
                                   ImVec2 size) {
    auto &cache = session.ui.viewportPickCache;
    if (cacheMatches(cache, session, vertices.size(), camera, origin, size))
        return cache;
    cache.clear();
    cache.revision = session.revision;
    cache.frameRevision = session.preview.frameRevision;
    cache.cameraYaw = camera.yaw;
    cache.cameraPitch = camera.pitch;
    cache.cameraDistance = camera.distance;
    cache.cameraTargetX = camera.target[0];
    cache.cameraTargetY = camera.target[1];
    cache.cameraTargetZ = camera.target[2];
    cache.orthographic = camera.orthographic;
    cache.originX = origin.x;
    cache.originY = origin.y;
    cache.width = size.x;
    cache.height = size.y;
    cache.vertices.reserve(vertices.size());
    for (const auto &vertex : vertices) {
        const auto point = projectWorldToScreen(camera, vertex.position, origin.x,
                                                origin.y, size.x, size.y);
        cache.vertices.push_back({point.x, point.y, point.depth, point.inFront});
    }

    const auto &model = session.document.model();
    const auto faceCount = model.indices.size() / 3U;
    cache.faceMaterial.resize(faceCount);
    std::size_t material = 0;
    std::size_t materialEnd = model.materials.empty() ? 0U : model.materials[0].indexCount;
    for (std::size_t face = 0; face < faceCount; ++face) {
        const auto offset = face * 3U;
        while (material + 1U < model.materials.size() && offset >= materialEnd) {
            ++material;
            materialEnd += model.materials[material].indexCount;
        }
        cache.faceMaterial[face] = static_cast<std::uint32_t>(material);
    }

    cache.grid.resize(ViewportPickCache::gridWidth * ViewportPickCache::gridHeight);
    const auto toCellX = [&](float x) {
        return static_cast<std::size_t>(std::clamp(
            (x - origin.x) / std::max(size.x, 1.0F) *
                static_cast<float>(ViewportPickCache::gridWidth),
            0.0F, static_cast<float>(ViewportPickCache::gridWidth - 1U)));
    };
    const auto toCellY = [&](float y) {
        return static_cast<std::size_t>(std::clamp(
            (y - origin.y) / std::max(size.y, 1.0F) *
                static_cast<float>(ViewportPickCache::gridHeight),
            0.0F, static_cast<float>(ViewportPickCache::gridHeight - 1U)));
    };
    for (std::size_t face = 0; face < faceCount; ++face) {
        const auto offset = face * 3U;
        const auto first = static_cast<std::size_t>(model.indices[offset]);
        const auto second = static_cast<std::size_t>(model.indices[offset + 1U]);
        const auto third = static_cast<std::size_t>(model.indices[offset + 2U]);
        if (first >= cache.vertices.size() || second >= cache.vertices.size() ||
            third >= cache.vertices.size() || !cache.vertices[first].inFront ||
            !cache.vertices[second].inFront || !cache.vertices[third].inFront)
            continue;
        const auto minimumX = std::min({cache.vertices[first].x, cache.vertices[second].x,
                                        cache.vertices[third].x});
        const auto maximumX = std::max({cache.vertices[first].x, cache.vertices[second].x,
                                        cache.vertices[third].x});
        const auto minimumY = std::min({cache.vertices[first].y, cache.vertices[second].y,
                                        cache.vertices[third].y});
        const auto maximumY = std::max({cache.vertices[first].y, cache.vertices[second].y,
                                        cache.vertices[third].y});
        for (std::size_t y = toCellY(minimumY); y <= toCellY(maximumY); ++y)
            for (std::size_t x = toCellX(minimumX); x <= toCellX(maximumX); ++x)
                cache.grid[y * ViewportPickCache::gridWidth + x].push_back(
                    static_cast<std::uint32_t>(face));
    }
    return cache;
}

std::span<const std::uint32_t> candidatesAt(const ViewportPickCache &cache,
                                             ImVec2 origin, ImVec2 size,
                                             ImVec2 mouse) {
    if (mouse.x < origin.x || mouse.y < origin.y || mouse.x > origin.x + size.x ||
        mouse.y > origin.y + size.y)
        return {};
    const auto x = static_cast<std::size_t>(std::clamp(
        (mouse.x - origin.x) / std::max(size.x, 1.0F) *
            static_cast<float>(ViewportPickCache::gridWidth),
        0.0F, static_cast<float>(ViewportPickCache::gridWidth - 1U)));
    const auto y = static_cast<std::size_t>(std::clamp(
        (mouse.y - origin.y) / std::max(size.y, 1.0F) *
            static_cast<float>(ViewportPickCache::gridHeight),
        0.0F, static_cast<float>(ViewportPickCache::gridHeight - 1U)));
    const auto &cell = cache.grid[y * ViewportPickCache::gridWidth + x];
    return {cell.data(), cell.size()};
}

bool materialVisible(const DocumentSession &session, std::uint32_t index) {
    const auto &materials = session.document.model().materials;
    if (index >= materials.size())
        return false;
    const auto handle = session.document.materialHandle(index);
    const SelectionItem item{SelectionKind::material, handle.domain, handle.id,
                             handle.generation};
    const auto contains = [&](const std::vector<SelectionItem> &items) {
        return std::find(items.begin(), items.end(), item) != items.end();
    };
    return !contains(session.ui.hiddenMaterials) &&
           (session.ui.isolatedMaterials.empty() ||
            contains(session.ui.isolatedMaterials));
}

SelectionItem itemFor(const DocumentSession &session, SelectionKind kind,
                      std::size_t index) {
    const auto make = [kind](const auto &handle) {
        return SelectionItem{kind, handle.domain, handle.id,
                             handle.generation};
    };
    switch (kind) {
    case SelectionKind::vertex:
        return make(session.document.vertexHandle(index));
    case SelectionKind::material:
        return make(session.document.materialHandle(index));
    case SelectionKind::bone:
        return make(session.document.boneHandle(index));
    case SelectionKind::rigidBody:
        return make(session.document.rigidBodyHandle(index));
    case SelectionKind::joint:
        return make(session.document.jointHandle(index));
    case SelectionKind::face:
        return make(session.document.faceHandle(index));
    default:
        return {};
    }
}

} // namespace

std::optional<ViewportPickResult>
pickViewport(const DocumentSession &session,
             const std::vector<mmd::PmxVertex> &vertices,
             const CameraState &camera, ImVec2 origin, ImVec2 size,
             ImVec2 mouse) {
    const auto &model = session.document.model();
    const auto &cache = ensurePickCache(session, vertices, camera, origin, size);
    if (session.ui.selectionMode == ViewportSelectionMode::face ||
        session.ui.selectionMode == ViewportSelectionMode::material) {
        std::optional<std::size_t> hit;
        float hitDepth = std::numeric_limits<float>::max();
        for (const auto faceValue : candidatesAt(cache, origin, size, mouse)) {
            const auto face = static_cast<std::size_t>(faceValue);
            if (face >= cache.faceMaterial.size() ||
                !materialVisible(session, cache.faceMaterial[face]))
                continue;
            const auto offset = face * 3U;
            const auto firstIndex =
                static_cast<std::size_t>(model.indices[offset]);
            const auto secondIndex =
                static_cast<std::size_t>(model.indices[offset + 1U]);
            const auto thirdIndex =
                static_cast<std::size_t>(model.indices[offset + 2U]);
            if (firstIndex >= vertices.size() ||
                secondIndex >= vertices.size() || thirdIndex >= vertices.size())
                continue;
            const auto &first = cache.vertices[firstIndex];
            const auto &second = cache.vertices[secondIndex];
            const auto &third = cache.vertices[thirdIndex];
            if (!first.inFront || !second.inFront || !third.inFront)
                continue;
            const auto area = (second.x - first.x) * (third.y - first.y) -
                              (second.y - first.y) * (third.x - first.x);
            if (std::abs(area) < 1e-6F)
                continue;
            const auto wa = ((second.x - mouse.x) * (third.y - mouse.y) -
                             (second.y - mouse.y) * (third.x - mouse.x)) /
                            area;
            const auto wb = ((third.x - mouse.x) * (first.y - mouse.y) -
                             (third.y - mouse.y) * (first.x - mouse.x)) /
                            area;
            const auto depth =
                wa * first.depth + wb * second.depth + (1.0F - wa - wb) * third.depth;
            if (depth < hitDepth &&
                insideTriangle(mouse, {first.x, first.y}, {second.x, second.y},
                               {third.x, third.y})) {
                hit = face;
                hitDepth = depth;
            }
        }
        if (!hit)
            return std::nullopt;
        const auto offset = *hit * 3U;
        const auto first = static_cast<std::size_t>(model.indices[offset]);
        const auto second = static_cast<std::size_t>(model.indices[offset + 1U]);
        const auto third = static_cast<std::size_t>(model.indices[offset + 2U]);
        const mmd::Float3 center{
            (vertices[first].position[0] + vertices[second].position[0] +
             vertices[third].position[0]) /
                3.0F,
            (vertices[first].position[1] + vertices[second].position[1] +
             vertices[third].position[1]) /
                3.0F,
            (vertices[first].position[2] + vertices[second].position[2] +
             vertices[third].position[2]) /
                3.0F};
        if (session.ui.selectionMode == ViewportSelectionMode::face)
            return ViewportPickResult{
                itemFor(session, SelectionKind::face, *hit), *hit, *hit,
                center};
        if (model.materials.empty())
            return std::nullopt;
        const auto material = cache.faceMaterial[*hit];
        return ViewportPickResult{
            itemFor(session, SelectionKind::material, material), material, *hit,
            center};
    }

    std::size_t closest{};
    mmd::Float3 closestPosition{};
    float distanceSquared = std::numeric_limits<float>::max();
    const auto consider = [&](const mmd::Float3 &position, std::size_t index) {
        const auto point = projectWorldToScreen(camera, position, origin.x,
                                                origin.y, size.x, size.y);
        if (!point.inFront)
            return;
        const auto dx = point.x - mouse.x;
        const auto dy = point.y - mouse.y;
        const auto candidate = dx * dx + dy * dy;
        if (candidate < distanceSquared) {
            distanceSquared = candidate;
            closest = index;
            closestPosition = position;
        }
    };
    SelectionKind kind{};
    if (session.ui.selectionMode == ViewportSelectionMode::vertex) {
        kind = SelectionKind::vertex;
        for (std::size_t index = 0; index < cache.vertices.size(); ++index) {
            const auto &point = cache.vertices[index];
            if (!point.inFront)
                continue;
            const auto dx = point.x - mouse.x;
            const auto dy = point.y - mouse.y;
            const auto candidate = dx * dx + dy * dy;
            if (candidate < distanceSquared) {
                distanceSquared = candidate;
                closest = index;
                closestPosition = vertices[index].position;
            }
        }
    } else if (session.ui.selectionMode == ViewportSelectionMode::bone) {
        kind = SelectionKind::bone;
        for (std::size_t index = 0; index < model.bones.size(); ++index)
            consider(evaluatedBonePosition(session, index, session.ui.previewFrame), index);
    } else if (session.ui.selectionMode == ViewportSelectionMode::rigidBody) {
        kind = SelectionKind::rigidBody;
        for (std::size_t index = 0; index < model.rigidBodies.size(); ++index)
            consider(model.rigidBodies[index].position, index);
    } else if (session.ui.selectionMode == ViewportSelectionMode::joint) {
        kind = SelectionKind::joint;
        for (std::size_t index = 0; index < model.joints.size(); ++index)
            consider(model.joints[index].position, index);
    }
    if (distanceSquared > 18.0F * 18.0F)
        return std::nullopt;
    return ViewportPickResult{itemFor(session, kind, closest), closest,
                              std::nullopt, closestPosition};
}

std::vector<ViewportPickResult>
pickViewportRectangle(const DocumentSession &session,
                      const std::vector<mmd::PmxVertex> &vertices,
                      const CameraState &camera, ImVec2 origin, ImVec2 size,
                      ImVec2 first, ImVec2 second) {
    const auto minimum = ImVec2{std::min(first.x, second.x), std::min(first.y, second.y)};
    const auto maximum = ImVec2{std::max(first.x, second.x), std::max(first.y, second.y)};
    const auto &cache = ensurePickCache(session, vertices, camera, origin, size);
    const auto contains = [&](const mmd::Float3 &position) {
        const auto point = projectWorldToScreen(camera, position, origin.x, origin.y, size.x, size.y);
        return point.inFront && point.x >= minimum.x && point.x <= maximum.x &&
               point.y >= minimum.y && point.y <= maximum.y;
    };
    std::vector<ViewportPickResult> result;
    const auto &model = session.document.model();
    if (session.ui.selectionMode == ViewportSelectionMode::vertex) {
        for (std::size_t index = 0; index < cache.vertices.size(); ++index)
            if (cache.vertices[index].inFront && cache.vertices[index].x >= minimum.x &&
                cache.vertices[index].x <= maximum.x && cache.vertices[index].y >= minimum.y &&
                cache.vertices[index].y <= maximum.y)
                result.push_back({itemFor(session, SelectionKind::vertex, index), index, std::nullopt,
                                  vertices[index].position});
    } else if (session.ui.selectionMode == ViewportSelectionMode::bone) {
        for (std::size_t index = 0; index < model.bones.size(); ++index)
            if (contains(evaluatedBonePosition(session, index, session.ui.previewFrame)))
                result.push_back({itemFor(session, SelectionKind::bone, index), index, std::nullopt,
                                  evaluatedBonePosition(session, index, session.ui.previewFrame)});
    } else if (session.ui.selectionMode == ViewportSelectionMode::rigidBody) {
        for (std::size_t index = 0; index < model.rigidBodies.size(); ++index)
            if (contains(model.rigidBodies[index].position))
                result.push_back({itemFor(session, SelectionKind::rigidBody, index), index, std::nullopt,
                                  model.rigidBodies[index].position});
    } else if (session.ui.selectionMode == ViewportSelectionMode::joint) {
        for (std::size_t index = 0; index < model.joints.size(); ++index)
            if (contains(model.joints[index].position))
                result.push_back({itemFor(session, SelectionKind::joint, index), index, std::nullopt,
                                  model.joints[index].position});
    } else {
        std::vector<bool> materials(model.materials.size());
        for (std::size_t face = 0; face < model.indices.size() / 3U; ++face) {
            const auto offset = face * 3U;
            const auto a = static_cast<std::size_t>(model.indices[offset]);
            const auto b = static_cast<std::size_t>(model.indices[offset + 1U]);
            const auto c = static_cast<std::size_t>(model.indices[offset + 2U]);
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
                continue;
            const mmd::Float3 center{(vertices[a].position[0] + vertices[b].position[0] + vertices[c].position[0]) / 3.0F,
                                     (vertices[a].position[1] + vertices[b].position[1] + vertices[c].position[1]) / 3.0F,
                                     (vertices[a].position[2] + vertices[b].position[2] + vertices[c].position[2]) / 3.0F};
            if (!contains(center))
                continue;
            const auto material = cache.faceMaterial[face];
            if (!materialVisible(session, material))
                continue;
            if (session.ui.selectionMode == ViewportSelectionMode::face) {
                result.push_back({itemFor(session, SelectionKind::face, face), face, face, center});
            } else if (!model.materials.empty()) {
                if (!materials[material]) {
                    materials[material] = true;
                    result.push_back({itemFor(session, SelectionKind::material, material), material, face, center});
                }
            }
        }
    }
    return result;
}

std::vector<ViewportPickResult> pickViewportRegion(const DocumentSession &session,
                                                   const std::vector<mmd::PmxVertex> &vertices,
                                                   const CameraState &camera, ImVec2 origin,
                                                   ImVec2 size, const std::vector<ImVec2> &polygon,
                                                   bool through) {
    if (polygon.size() < 3)
        return {};
    const auto &cache = ensurePickCache(session, vertices, camera, origin, size);
    const auto contains = [&](ImVec2 p) {
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            const auto a = polygon[i], b = polygon[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                inside = !inside;
        }
        return inside;
    };
    const auto intersection = [](ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d) -> std::optional<ImVec2> {
        const auto rx = b.x - a.x, ry = b.y - a.y, sx = d.x - c.x, sy = d.y - c.y;
        const auto denominator = rx * sy - ry * sx;
        if (std::abs(denominator) < 1e-6F)
            return std::nullopt;
        const auto t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / denominator;
        const auto u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / denominator;
        if (t < 0 || t > 1 || u < 0 || u > 1)
            return std::nullopt;
        return ImVec2{a.x + t * rx, a.y + t * ry};
    };
    float minX = polygon[0].x, maxX = minX, minY = polygon[0].y, maxY = minY;
    for (auto p : polygon) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    std::vector<ViewportPickResult> result;
    const auto &model = session.document.model();
    for (std::size_t face = 0; face < model.indices.size() / 3U; ++face) {
        if (!materialVisible(session, cache.faceMaterial[face]))
            continue;
        std::array<ImVec2, 3> points;
        bool valid = true;
        for (std::size_t v = 0; v < 3; ++v) {
            const auto index = model.indices[face * 3 + v];
            if (index >= cache.vertices.size() || !cache.vertices[index].inFront) {
                valid = false;
                break;
            }
            points[v] = {cache.vertices[index].x, cache.vertices[index].y};
        }
        if (!valid || std::max({points[0].x, points[1].x, points[2].x}) < minX ||
            std::min({points[0].x, points[1].x, points[2].x}) > maxX ||
            std::max({points[0].y, points[1].y, points[2].y}) < minY ||
            std::min({points[0].y, points[1].y, points[2].y}) > maxY)
            continue;
        const ImVec2 center{(points[0].x + points[1].x + points[2].x) / 3,
                            (points[0].y + points[1].y + points[2].y) / 3};
        std::vector<ImVec2> samples;
        if (contains(center))
            samples.push_back(center);
        for (auto p : points)
            if (contains(p))
                samples.push_back(p);
        for (auto p : polygon)
            if (insideTriangle(p, points[0], points[1], points[2]))
                samples.push_back(p);
        for (std::size_t edge = 0; edge < 3; ++edge)
            for (std::size_t i = 0; i < polygon.size(); ++i)
                if (auto p = intersection(points[edge], points[(edge + 1) % 3], polygon[i],
                                          polygon[(i + 1) % polygon.size()]))
                    samples.push_back(*p);
        if (samples.empty())
            continue;
        bool visible = through;
        if (!through)
            for (auto sample : samples) {
                // Inset boundary samples to avoid choosing the neighboring triangle on a shared
                // edge.
                sample.x = sample.x * 0.999F + center.x * 0.001F;
                sample.y = sample.y * 0.999F + center.y * 0.001F;
                const auto hit = pickViewport(session, vertices, camera, origin, size, sample);
                if (hit && hit->face == face) {
                    visible = true;
                    break;
                }
            }
        if (visible) {
            mmd::Float3 world{};
            for (std::size_t v = 0; v < 3; ++v)
                for (std::size_t axis = 0; axis < 3; ++axis)
                    world[axis] += vertices[model.indices[face * 3 + v]].position[axis] / 3;
            result.push_back({itemFor(session, SelectionKind::face, face), face, face, world});
        }
    }
    return result;
}

} // namespace pmxer
