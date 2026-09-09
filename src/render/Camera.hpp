#pragma once

#include <mmd/pmx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace pmxer {

struct CameraState {
    mmd::Float3 target{};
    float yaw{};
    float pitch{};
    float distance{3.0F};
    bool orthographic{};
};

struct CameraMatrices {
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::array<float, 16> viewProjection{};
};

struct ScreenPoint {
    float x{};
    float y{};
    bool inFront{};
    float depth{};
};

inline mmd::Float3 cameraEye(const CameraState &camera) {
    const auto cosPitch = std::cos(camera.pitch);
    const auto sinPitch = std::sin(camera.pitch);
    const auto cosYaw = std::cos(camera.yaw);
    const auto sinYaw = std::sin(camera.yaw);
    return {camera.target[0] + sinYaw * cosPitch * camera.distance,
            camera.target[1] + sinPitch * camera.distance,
            camera.target[2] + cosYaw * cosPitch * camera.distance};
}

inline CameraMatrices makeCameraMatrices(const CameraState &camera, float aspect) {
    const auto eye = cameraEye(camera);
    auto forward = mmd::Float3{camera.target[0] - eye[0], camera.target[1] - eye[1], camera.target[2] - eye[2]};
    const auto length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length > std::numeric_limits<float>::epsilon())
        for (auto &component : forward)
            component /= length;
    const mmd::Float3 upAxis{0.0F, 1.0F, 0.0F};
    auto right = mmd::Float3{forward[1] * upAxis[2] - forward[2] * upAxis[1],
                             forward[2] * upAxis[0] - forward[0] * upAxis[2],
                             forward[0] * upAxis[1] - forward[1] * upAxis[0]};
    const auto rightLength = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rightLength > std::numeric_limits<float>::epsilon())
        for (auto &component : right)
            component /= rightLength;
    auto up = mmd::Float3{right[1] * forward[2] - right[2] * forward[1],
                          right[2] * forward[0] - right[0] * forward[2],
                          right[0] * forward[1] - right[1] * forward[0]};
    const auto upLength = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (upLength > std::numeric_limits<float>::epsilon())
        for (auto &component : up)
            component /= upLength;
    const auto dot = [](const mmd::Float3 &left, const mmd::Float3 &rightValue) {
        return left[0] * rightValue[0] + left[1] * rightValue[1] + left[2] * rightValue[2];
    };
    const auto nearPlane = std::max(0.01F, camera.distance * 0.001F);
    const auto farPlane = std::max(1000.0F, camera.distance * 100.0F);
    const auto focal = 1.0F / std::tan(0.75F * 0.5F);
    const auto safeAspect = std::max(aspect, 0.001F);
    const auto halfHeight = std::max(camera.distance * std::tan(0.75F * 0.5F), 0.001F);
    const auto xScale = camera.orthographic ? 1.0F / (halfHeight * safeAspect) : focal / safeAspect;
    const auto yScale = camera.orthographic ? 1.0F / halfHeight : focal;
    const auto zScale = farPlane / (farPlane - nearPlane);
    const auto zOffset = -nearPlane * farPlane / (farPlane - nearPlane);
    const std::array<float, 16> view{
        right[0], right[1], right[2], -dot(right, eye),
        up[0], up[1], up[2], -dot(up, eye),
        forward[0], forward[1], forward[2], -dot(forward, eye),
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    const std::array<float, 16> perspective{
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, zScale, zOffset,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
    const std::array<float, 16> orthographic{
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F / (farPlane - nearPlane), -nearPlane / (farPlane - nearPlane),
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    const auto &projection = camera.orthographic ? orthographic : perspective;
    CameraMatrices result;
    result.view = view;
    result.projection = projection;
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            result.viewProjection[row * 4 + column] =
                projection[row * 4 + 0] * view[column] + projection[row * 4 + 1] * view[4 + column] +
                projection[row * 4 + 2] * view[8 + column] + projection[row * 4 + 3] * view[12 + column];
    return result;
}

inline ScreenPoint projectWorldToScreen(const CameraState &camera, const mmd::Float3 &position, float originX,
                                        float originY, float width, float height) {
    const auto matrices = makeCameraMatrices(camera, width / std::max(height, 0.001F));
    const auto &m = matrices.viewProjection;
    const auto x = m[0] * position[0] + m[1] * position[1] + m[2] * position[2] + m[3];
    const auto y = m[4] * position[0] + m[5] * position[1] + m[6] * position[2] + m[7];
    const auto z = m[8] * position[0] + m[9] * position[1] + m[10] * position[2] + m[11];
    const auto w = m[12] * position[0] + m[13] * position[1] + m[14] * position[2] + m[15];
    if (std::abs(w) <= std::numeric_limits<float>::epsilon())
        return {originX, originY, false, 0.0F};
    const auto ndcX = x / w;
    const auto ndcY = y / w;
    const auto depth = z / w;
    return {originX + (ndcX * 0.5F + 0.5F) * width,
            originY + (0.5F - ndcY * 0.5F) * height,
            w > 0.0F && depth >= 0.0F && depth <= 1.0F,
            depth};
}

} // namespace pmxer
