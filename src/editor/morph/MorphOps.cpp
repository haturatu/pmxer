#include "MorphOps.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>

namespace pmxer::morph {
namespace {

constexpr mmd::Float4 identityQuaternion{0.0F, 0.0F, 0.0F, 1.0F};

void scale3(mmd::Float3 &value, float factor) {
    for (auto &component : value)
        component *= factor;
}

void scale4(mmd::Float4 &value, float factor) {
    for (auto &component : value)
        component *= factor;
}

mmd::Float4 normalizeQuaternion(mmd::Float4 value) {
    float length{};
    for (const auto component : value)
        length += component * component;
    if (length <= 1e-12F)
        return identityQuaternion;
    const auto inverse = 1.0F / std::sqrt(length);
    for (auto &component : value)
        component *= inverse;
    return value;
}

mmd::Float4 multiplyQuaternion(const mmd::Float4 &lhs, const mmd::Float4 &rhs) {
    return normalizeQuaternion({
        lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
        lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
        lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2],
    });
}

mmd::Float4 inverseQuaternion(const mmd::Float4 &value) {
    const auto normalized = normalizeQuaternion(value);
    return {-normalized[0], -normalized[1], -normalized[2], normalized[3]};
}

mmd::Float4 slerpIdentity(const mmd::Float4 &value, float factor) {
    auto target = normalizeQuaternion(value);
    auto dot = target[3];
    if (dot < 0.0F) {
        for (auto &component : target)
            component = -component;
        dot = -dot;
    }
    if (dot > 0.9995F) {
        auto result = identityQuaternion;
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] += (target[index] - result[index]) * factor;
        return normalizeQuaternion(result);
    }
    const auto angle = std::acos(std::clamp(dot, -1.0F, 1.0F));
    const auto sine = std::sin(angle);
    if (std::abs(sine) <= 1e-6F)
        return identityQuaternion;
    const auto first = std::sin((1.0F - factor) * angle) / sine;
    const auto second = std::sin(factor * angle) / sine;
    mmd::Float4 result{};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = identityQuaternion[index] * first + target[index] * second;
    return normalizeQuaternion(result);
}

struct OffsetKey {
    std::int32_t index{};
    std::int32_t secondaryIndex{};
    std::uint8_t operation{};
    bool local{};
    auto operator<=>(const OffsetKey &) const = default;
};

OffsetKey keyFor(const mmd::PmxMorphOffset &offset) {
    return {offset.index, offset.secondaryIndex, offset.operation, offset.local};
}

void addOffset(mmd::PmxMorphOffset &destination, const mmd::PmxMorphOffset &source,
               std::uint8_t type) {
    if (type == 0U || type == 9U) {
        destination.scalar += source.scalar;
        return;
    }
    if (type == 1U || type == 2U) {
        for (std::size_t index = 0; index < destination.vector3.size(); ++index)
            destination.vector3[index] += source.vector3[index];
        if (type == 2U)
            destination.vector4 = multiplyQuaternion(
                normalizeQuaternion(destination.vector4), normalizeQuaternion(source.vector4));
        return;
    }
    if (type >= 3U && type <= 7U) {
        for (std::size_t index = 0; index < destination.vector4.size(); ++index)
            destination.vector4[index] += source.vector4[index];
        return;
    }
    if (type == 8U) {
        for (std::size_t vector = 0; vector < destination.materialVectors.size(); ++vector)
            for (std::size_t component = 0; component < destination.materialVectors[vector].size(); ++component)
                if (destination.operation == 0U)
                    destination.materialVectors[vector][component] *= source.materialVectors[vector][component];
                else
                    destination.materialVectors[vector][component] += source.materialVectors[vector][component];
        return;
    }
    if (type == 10U) {
        scale3(destination.vector3, 1.0F);
        scale3(destination.tertiaryVector3, 1.0F);
        for (std::size_t index = 0; index < destination.vector3.size(); ++index) {
            destination.vector3[index] += source.vector3[index];
            destination.tertiaryVector3[index] += source.tertiaryVector3[index];
        }
    }
}

bool nearZero(float value) {
    return std::abs(value) <= 1e-7F;
}

bool neutralOffset(const MorphData &morph, const mmd::PmxMorphOffset &offset) {
    if (morph.type == 0U || morph.type == 9U)
        return nearZero(offset.scalar);
    if (morph.type == 1U)
        return std::all_of(offset.vector3.begin(), offset.vector3.end(), nearZero);
    if (morph.type == 2U) {
        const auto quaternion = normalizeQuaternion(offset.vector4);
        return std::all_of(offset.vector3.begin(), offset.vector3.end(), nearZero) &&
               nearZero(quaternion[0]) && nearZero(quaternion[1]) && nearZero(quaternion[2]) &&
               nearZero(std::abs(quaternion[3]) - 1.0F);
    }
    if (morph.type >= 3U && morph.type <= 7U)
        return std::all_of(offset.vector4.begin(), offset.vector4.end(), nearZero);
    if (morph.type == 8U) {
        const auto neutral = offset.operation == 0U ? 1.0F : 0.0F;
        for (std::size_t vector = 0; vector < 7U; ++vector)
            if (!std::all_of(offset.materialVectors[vector].begin(),
                             offset.materialVectors[vector].end(),
                             [&](float value) { return nearZero(value - neutral); }))
                return false;
        return true;
    }
    if (morph.type == 10U)
        return std::all_of(offset.vector3.begin(), offset.vector3.end(), nearZero) &&
               std::all_of(offset.tertiaryVector3.begin(), offset.tertiaryVector3.end(), nearZero);
    return false;
}

} // namespace

MorphData copy(const mmd::PmxMorph &morph) {
    return {morph.type, morph.offsets, morph.panel, morph.englishName};
}

MorphData scale(MorphData value, float factor) {
    for (auto &offset : value.offsets) {
        if (value.type == 0U || value.type == 9U) {
            offset.scalar *= factor;
        } else if (value.type == 1U) {
            scale3(offset.vector3, factor);
        } else if (value.type == 2U) {
            scale3(offset.vector3, factor);
            offset.vector4 = slerpIdentity(offset.vector4, factor);
        } else if (value.type >= 3U && value.type <= 7U) {
            scale4(offset.vector4, factor);
        } else if (value.type == 8U) {
            for (std::size_t vector = 0; vector < offset.materialVectors.size(); ++vector) {
                for (auto &component : offset.materialVectors[vector]) {
                    if (offset.operation == 0U)
                        component = 1.0F + (component - 1.0F) * factor;
                    else
                        component *= factor;
                }
            }
        } else if (value.type == 10U) {
            scale3(offset.vector3, factor);
            scale3(offset.tertiaryVector3, factor);
        }
    }
    return pruneZeroOffsets(std::move(value));
}

InvertResult invert(MorphData value) {
    for (auto &offset : value.offsets) {
        if (value.type == 2U) {
            scale3(offset.vector3, -1.0F);
            offset.vector4 = inverseQuaternion(offset.vector4);
        } else if (value.type == 8U) {
            for (auto &values : offset.materialVectors) {
                for (auto &component : values) {
                    if (offset.operation == 0U) {
                        if (std::abs(component) <= 1e-6F)
                            return {false, {},
                                    "Material multiply morph contains a zero factor and cannot be inverted exactly."};
                        component = 1.0F / component;
                    } else {
                        component = -component;
                    }
                }
            }
        } else {
            offset.scalar = -offset.scalar;
            scale3(offset.vector3, -1.0F);
            scale4(offset.vector4, -1.0F);
            scale3(offset.tertiaryVector3, -1.0F);
        }
    }
    return {true, pruneZeroOffsets(std::move(value)), {}};
}

MorphData pruneZeroOffsets(MorphData value) {
    std::erase_if(value.offsets, [&](const auto &offset) { return neutralOffset(value, offset); });
    return value;
}

MorphData duplicate(const MorphData &value) {
    return value;
}

MorphData combine(std::span<const MorphData> values) {
    MorphData result;
    bool initialized = false;
    for (const auto &value : values) {
        if (!initialized) {
            result.type = value.type;
            result.panel = value.panel;
            result.englishName = value.englishName;
            initialized = true;
        }
        if (value.type != result.type)
            continue;
        std::map<OffsetKey, std::size_t> offsets;
        for (std::size_t index = 0; index < result.offsets.size(); ++index)
            offsets.emplace(keyFor(result.offsets[index]), index);
        for (const auto &offset : value.offsets) {
            const auto [found, inserted] = offsets.emplace(keyFor(offset), result.offsets.size());
            if (inserted)
                result.offsets.push_back(offset);
            else
                addOffset(result.offsets[found->second], offset, result.type);
        }
    }
    return pruneZeroOffsets(std::move(result));
}

MorphData subtract(const MorphData &lhs, const MorphData &rhs) {
    const auto inverted = invert(duplicate(rhs));
    if (!inverted.success)
        return {};
    const std::array<MorphData, 2> values{lhs, inverted.data};
    return combine(values);
}

SideSplitResult splitSide(const mmd::PmxModel &model, const mmd::PmxMorph &morph,
                          SideSplitOptions options) {
    SideSplitResult result{{morph.type, {}, morph.panel, morph.englishName},
                           {morph.type, {}, morph.panel, morph.englishName}};
    if (morph.type != 1U)
        return {copy(morph), {morph.type, {}, 4U, {}}};
    const auto feather = std::max(std::abs(options.feather), 0.0F);
    for (const auto &offset : morph.offsets) {
        if (offset.index < 0 || static_cast<std::size_t>(offset.index) >= model.vertices.size())
            continue;
        const auto x = model.vertices[static_cast<std::size_t>(offset.index)].position[0];
        float rightWeight{};
        if (feather <= 1e-6F)
            rightWeight = x > options.centerX ? 1.0F : 0.0F;
        else {
            const auto normalized = std::clamp((x - options.centerX + feather) / (2.0F * feather), 0.0F, 1.0F);
            rightWeight = normalized * normalized * (3.0F - 2.0F * normalized);
        }
        auto leftWeight = 1.0F - rightWeight;
        if (options.duplicateCenterVertices && std::abs(x - options.centerX) <= feather)
            leftWeight = rightWeight = 1.0F;
        if (options.swapSides)
            std::swap(leftWeight, rightWeight);
        if (leftWeight > 1e-6F) {
            auto left = offset;
            scale3(left.vector3, leftWeight);
            result.left.offsets.push_back(left);
        }
        if (rightWeight > 1e-6F) {
            auto right = offset;
            scale3(right.vector3, rightWeight);
            result.right.offsets.push_back(right);
        }
    }
    return result;
}

} // namespace pmxer::morph
