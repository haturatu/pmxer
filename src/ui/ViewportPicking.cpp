#include "ViewportPicking.hpp"

#include "../editor/DocumentSession.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
    return !((first < 0.0F || second < 0.0F || third < 0.0F) &&
             (first > 0.0F || second > 0.0F || third > 0.0F));
}

std::size_t materialForIndex(const mmd::PmxModel &model,
                             std::size_t indexOffset) {
    std::size_t end{};
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        end += model.materials[index].indexCount;
        if (indexOffset < end)
            return index;
    }
    return model.materials.empty() ? 0U : model.materials.size() - 1U;
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
    if (session.ui.selectionMode == ViewportSelectionMode::face ||
        session.ui.selectionMode == ViewportSelectionMode::material) {
        std::optional<std::size_t> hit;
        float hitDepth = std::numeric_limits<float>::max();
        for (std::size_t face = 0; face < model.indices.size() / 3U; ++face) {
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
            const auto first = projectWorldToScreen(
                camera, vertices[firstIndex].position, origin.x, origin.y,
                size.x, size.y);
            const auto second = projectWorldToScreen(
                camera, vertices[secondIndex].position, origin.x, origin.y,
                size.x, size.y);
            const auto third = projectWorldToScreen(
                camera, vertices[thirdIndex].position, origin.x, origin.y,
                size.x, size.y);
            if (!first.inFront || !second.inFront || !third.inFront)
                continue;
            const auto depth = (first.depth + second.depth + third.depth) / 3.0F;
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
        const auto material = materialForIndex(model, offset);
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
        for (std::size_t index = 0; index < vertices.size(); ++index)
            consider(vertices[index].position, index);
    } else if (session.ui.selectionMode == ViewportSelectionMode::bone) {
        kind = SelectionKind::bone;
        for (std::size_t index = 0; index < model.bones.size(); ++index)
            consider(model.bones[index].position, index);
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
    const auto contains = [&](const mmd::Float3 &position) {
        const auto point = projectWorldToScreen(camera, position, origin.x, origin.y, size.x, size.y);
        return point.inFront && point.x >= minimum.x && point.x <= maximum.x &&
               point.y >= minimum.y && point.y <= maximum.y;
    };
    std::vector<ViewportPickResult> result;
    const auto &model = session.document.model();
    if (session.ui.selectionMode == ViewportSelectionMode::vertex) {
        for (std::size_t index = 0; index < vertices.size(); ++index)
            if (contains(vertices[index].position))
                result.push_back({itemFor(session, SelectionKind::vertex, index), index, std::nullopt,
                                  vertices[index].position});
    } else if (session.ui.selectionMode == ViewportSelectionMode::bone) {
        for (std::size_t index = 0; index < model.bones.size(); ++index)
            if (contains(model.bones[index].position))
                result.push_back({itemFor(session, SelectionKind::bone, index), index, std::nullopt,
                                  model.bones[index].position});
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
            if (session.ui.selectionMode == ViewportSelectionMode::face) {
                result.push_back({itemFor(session, SelectionKind::face, face), face, face, center});
            } else if (!model.materials.empty()) {
                const auto material = materialForIndex(model, offset);
                if (!materials[material]) {
                    materials[material] = true;
                    result.push_back({itemFor(session, SelectionKind::material, material), material, face, center});
                }
            }
        }
    }
    return result;
}

} // namespace pmxer
