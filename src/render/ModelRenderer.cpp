#include "ModelRenderer.hpp"

namespace pmxer {

void ModelRenderer::setModel(const mmd::PmxModel &model) {
    model_ = &model;
    invalidation_ = {true, true, true, true, true, true};
}

void ModelRenderer::invalidate(RenderInvalidation invalidation) noexcept {
    invalidation_.topology = invalidation_.topology || invalidation.topology;
    invalidation_.materials = invalidation_.materials || invalidation.materials;
    invalidation_.bones = invalidation_.bones || invalidation.bones;
    invalidation_.morphs = invalidation_.morphs || invalidation.morphs;
    invalidation_.textures = invalidation_.textures || invalidation.textures;
    invalidation_.physics = invalidation_.physics || invalidation.physics;
}

void ModelRenderer::setFrame(const mmd::AnimatedModelFrame *frame) noexcept {
    frame_ = frame;
}

RenderStatistics ModelRenderer::statistics() const noexcept {
    if (model_ == nullptr)
        return {};
    return {model_->vertices.size(), model_->indices.size(), model_->materials.size(), model_->bones.size()};
}

bool ModelRenderer::hasModel() const noexcept {
    return model_ != nullptr;
}

} // namespace pmxer

