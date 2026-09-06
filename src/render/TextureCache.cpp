#include "TextureCache.hpp"

namespace pmxer {

void TextureCache::inspect(const mmd::PmxModel &model) {
    items_.clear();
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        const auto &texture = model.textures[i];
        const auto resolved = mmd::pmx::resolveTexturePath(model, i);
        items_.emplace(texture.storedPath, TextureStatus{texture.storedPath, resolved, std::filesystem::exists(resolved)});
    }
}

const std::unordered_map<std::string, TextureStatus> &TextureCache::items() const noexcept {
    return items_;
}

void TextureCache::clear() noexcept {
    items_.clear();
}

} // namespace pmxer

