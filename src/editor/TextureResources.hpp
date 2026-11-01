#pragma once
#include <filesystem>
#include <memory>
#include <mmd/pmx.hpp>
#include <vector>
namespace pmxer {
struct TextureResource {
    std::size_t index{};
    std::filesystem::path source, destination;
    bool exists{}, copied{};
};
// Serialization changes the reference base, not the editor's live resource
// origin. Keeping that origin stable also keeps undo snapshots valid.
mmd::PmxModel rebaseTexturePaths(const mmd::PmxModel &, const std::filesystem::path &destination);
struct SaveResourceSnapshot {
    SaveResourceSnapshot() = default;
    SaveResourceSnapshot(const SaveResourceSnapshot &) = delete;
    SaveResourceSnapshot &operator=(const SaveResourceSnapshot &) = delete;
    mmd::PmxModel model;
    std::vector<TextureResource> resources;
    std::filesystem::path directory;
    bool published{};
    ~SaveResourceSnapshot();
};
std::unique_ptr<SaveResourceSnapshot>
prepareSaveResources(const mmd::PmxModel &, const std::filesystem::path &, bool copyResources);
} // namespace pmxer
