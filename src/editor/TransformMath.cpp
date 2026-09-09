#include "TransformMath.hpp"

#include <cmath>

namespace pmxer {
namespace {

constexpr mmd::Float4 identityQuaternion{0.0F, 0.0F, 0.0F, 1.0F};

mmd::Float4 normalizeQuaternion(mmd::Float4 value) noexcept {
    float length{};
    for (const auto component : value)
        length += component * component;
    if (length <= 1e-12F)
        return identityQuaternion;
    for (auto &component : value)
        component /= std::sqrt(length);
    return value;
}

mmd::Float4 multiplyQuaternion(const mmd::Float4 &lhs,
                               const mmd::Float4 &rhs) noexcept {
    return normalizeQuaternion({
        lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
        lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
        lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2],
    });
}

} // namespace

mmd::Float4 quaternionFromEuler(const mmd::Float3 &euler) noexcept {
    const auto halfX = euler[0] * 0.5F;
    const auto halfY = euler[1] * 0.5F;
    const auto halfZ = euler[2] * 0.5F;
    const mmd::Float4 x{std::sin(halfX), 0.0F, 0.0F, std::cos(halfX)};
    const mmd::Float4 y{0.0F, std::sin(halfY), 0.0F, std::cos(halfY)};
    const mmd::Float4 z{0.0F, 0.0F, std::sin(halfZ), std::cos(halfZ)};
    return multiplyQuaternion(z, multiplyQuaternion(y, x));
}

std::array<float, 16> composeRotationMatrix(const mmd::Float4 &rotation) noexcept {
    const auto normalized = normalizeQuaternion(rotation);
    const auto x = normalized[0];
    const auto y = normalized[1];
    const auto z = normalized[2];
    const auto w = normalized[3];
    std::array<float, 16> matrix{};
    matrix[0] = 1.0F - 2.0F * (y * y + z * z);
    matrix[4] = 2.0F * (x * y - z * w);
    matrix[8] = 2.0F * (x * z + y * w);
    matrix[1] = 2.0F * (x * y + z * w);
    matrix[5] = 1.0F - 2.0F * (x * x + z * z);
    matrix[9] = 2.0F * (y * z - x * w);
    matrix[2] = 2.0F * (x * z - y * w);
    matrix[6] = 2.0F * (y * z + x * w);
    matrix[10] = 1.0F - 2.0F * (x * x + y * y);
    matrix[15] = 1.0F;
    return matrix;
}

} // namespace pmxer
