#include "TextureTool.hpp"

#include "../EditorOperations.hpp"

#include <filesystem>

namespace pmxer {

bool relinkTexture(DocumentSession &session, mmd::TextureHandle handle, std::string storedPath) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setTexturePath(handle, std::move(storedPath));
    }, "テクスチャを再リンク").success;
}

std::vector<std::size_t> missingTextures(const mmd::PmxModel &model) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < model.textures.size(); ++i)
        if (!std::filesystem::exists(mmd::pmx::resolveTexturePath(model, i)))
            result.push_back(i);
    return result;
}

bool convertAbsoluteTextures(DocumentSession &session, const std::filesystem::path &root) {
    return applyTransaction(session, [&](auto &transaction) {
        for (std::size_t i = 0; i < session.document.model().textures.size(); ++i) {
            const auto &texture = session.document.model().textures[i];
            const auto path = std::filesystem::path(texture.storedPath);
            if (!path.is_absolute())
                continue;
            const auto relative = path.lexically_relative(root);
            if (!transaction.setTexturePath(session.document.textureHandle(i), relative.generic_string()))
                return false;
        }
        return true;
    }, "テクスチャパスを相対化").success;
}

} // namespace pmxer

