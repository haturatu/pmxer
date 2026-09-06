#include "MaterialTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool setMaterialTexture(DocumentSession &session, mmd::MaterialHandle handle, std::optional<mmd::TextureHandle> texture) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.textureIndex = -1;
    if (texture) {
        for (std::size_t index = 0; index < session.document.model().textures.size(); ++index)
            if (session.document.textureHandle(index) == *texture)
                edited.textureIndex = static_cast<std::int32_t>(index);
    }
    return editMaterial(session, handle, edited).success;
}

bool setMaterialColors(DocumentSession &session, mmd::MaterialHandle handle, mmd::Float4 diffuse,
                       mmd::Float3 ambient) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.diffuse = diffuse;
    edited.ambient = ambient;
    return editMaterial(session, handle, edited).success;
}

bool reorderMaterial(DocumentSession &session, mmd::MaterialHandle handle, std::size_t destination) {
    return moveMaterial(session, handle, destination).success;
}

} // namespace pmxer
