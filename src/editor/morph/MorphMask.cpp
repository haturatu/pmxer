#include "MorphMask.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pmxer::morph {

MorphData filterByMaterial(const mmd::PmxModel &model, const mmd::PmxMorph &morph,
                           std::optional<std::size_t> material,
                           MaterialFilterMode mode) {
    if (!material || *material >= model.materials.size() || morph.type != 1U)
        return copy(morph);
    std::vector<std::uint8_t> selected(model.vertices.size());
    std::vector<std::size_t> firstMaterial(model.vertices.size(), model.materials.size());
    std::vector<std::uint8_t> usedByMultipleMaterials(model.vertices.size());
    std::size_t indexOffset{};
    for (std::size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex) {
        const auto count = static_cast<std::size_t>(model.materials[materialIndex].indexCount);
        const auto end = std::min(indexOffset + count, model.indices.size());
        for (std::size_t index = indexOffset; index < end; ++index) {
            const auto vertex = static_cast<std::size_t>(model.indices[index]);
            if (vertex >= model.vertices.size())
                continue;
            if (materialIndex == *material)
                selected[vertex] = 1U;
            if (firstMaterial[vertex] == model.materials.size())
                firstMaterial[vertex] = materialIndex;
            else if (firstMaterial[vertex] != materialIndex)
                usedByMultipleMaterials[vertex] = 1U;
        }
        indexOffset = end;
    }
    MorphData result{morph.type, {}};
    const auto keepOnly = mode == MaterialFilterMode::keepOnlyUsed || mode == MaterialFilterMode::keepOnlyExclusive;
    const auto exclusive = mode == MaterialFilterMode::excludeExclusive || mode == MaterialFilterMode::keepOnlyExclusive;
    for (const auto &offset : morph.offsets) {
        const auto vertex = offset.index < 0 ? model.vertices.size() : static_cast<std::size_t>(offset.index);
        const auto matches = vertex < selected.size() && selected[vertex] != 0U &&
                             (!exclusive || usedByMultipleMaterials[vertex] == 0U);
        if ((keepOnly && matches) || (!keepOnly && !matches))
            result.offsets.push_back(offset);
    }
    return result;
}

} // namespace pmxer::morph
