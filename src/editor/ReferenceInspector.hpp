#pragma once

#include <mmd/document.hpp>

#include <cstddef>

namespace pmxer {

struct ReferenceSummary {
    std::size_t vertices{};
    std::size_t childBones{};
    std::size_t ikLinks{};
    std::size_t morphs{};
    std::size_t displayFrames{};
    std::size_t rigidBodies{};
    std::size_t joints{};
    std::size_t softBodies{};
};

[[nodiscard]] ReferenceSummary summarizeReferences(const mmd::PmxDocument &document, mmd::BoneHandle handle);
[[nodiscard]] ReferenceSummary summarizeReferences(const mmd::PmxDocument &document, mmd::MaterialHandle handle);
[[nodiscard]] ReferenceSummary summarizeReferences(const mmd::PmxDocument &document, mmd::TextureHandle handle);

} // namespace pmxer

