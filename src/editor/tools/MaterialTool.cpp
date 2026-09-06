#include "MaterialTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool setMaterialTexture(DocumentSession &session, mmd::MaterialHandle handle, std::optional<mmd::TextureHandle> texture) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setMaterialTexture(handle, texture);
    }, "材質テクスチャを変更").success;
}

bool setMaterialColors(DocumentSession &session, mmd::MaterialHandle handle, mmd::Float4 diffuse,
                       mmd::Float3 ambient) {
    return applyTransaction(session, [&](auto &transaction) {
        if (!transaction.setMaterialDiffuse(handle, diffuse))
            return false;
        return transaction.setMaterialAmbient(handle, ambient);
    }, "材質色を変更").success;
}

bool reorderMaterial(DocumentSession &session, mmd::MaterialHandle handle, std::size_t destination) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.moveMaterial(handle, destination);
    }, "材質順序を変更").success;
}

} // namespace pmxer

