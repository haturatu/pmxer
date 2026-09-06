#include "../../src/editor/DocumentSession.hpp"
#include "../../src/editor/EditorOperations.hpp"
#include "../../src/editor/SaveController.hpp"
#include "../../src/render/Picking.hpp"

#include <mmd/pmx.hpp>

#include <cassert>
#include <filesystem>

namespace {

mmd::PmxModel sampleModel() {
    mmd::PmxModel model;
    model.metadata.version = 2.1F;
    model.metadata.modelName = "sample";
    mmd::PmxBone bone;
    bone.name = "root";
    bone.englishName = "root";
    model.bones.push_back(bone);
    mmd::PmxMaterial material;
    material.name = "material";
    material.indexCount = 3;
    model.materials.push_back(material);
    for (float x : {0.0F, 1.0F, 0.0F}) {
        mmd::PmxVertex vertex;
        vertex.position = {x, x == 1.0F ? 1.0F : 0.0F, 0.0F};
        vertex.normal = {0.0F, 0.0F, 1.0F};
        vertex.bones[0] = 0;
        model.vertices.push_back(vertex);
    }
    model.indices = {0, 1, 2};
    return model;
}

} // namespace

int main() {
    pmxer::DocumentSession session(sampleModel());
    assert(session.document.validate().valid());
    const auto handle = session.document.vertexHandle(0);
    auto changed = *session.document.resolve(handle);
    changed.position[0] = 2.0F;
    assert(pmxer::editVertex(session, handle, changed).success);
    assert(session.modified);
    assert(!session.changes.vertices.empty());
    assert(session.commands.undoCount() == 1);
    assert(session.commands.undo(session.document));
    assert(session.document.model().vertices[0].position[0] == 0.0F);
    assert(session.commands.redo(session.document));
    assert(session.document.model().vertices[0].position[0] == 2.0F);

    const auto materialHandle = session.document.materialHandle(0);
    auto material = *session.document.resolve(materialHandle);
    material.diffuse[0] = 0.25F;
    assert(pmxer::editMaterial(session, materialHandle, material).success);
    const auto boneHandle = session.document.boneHandle(0);
    auto bone = *session.document.resolve(boneHandle);
    bone.name = "edited";
    assert(pmxer::editBone(session, boneHandle, bone).success);

    auto skin = mmd::PmxVertexSkin{};
    skin.bones[0] = boneHandle;
    auto vertexTransaction = session.document.transaction();
    assert(vertexTransaction.setVertexSkin(handle, skin));
    const auto vertexResult = vertexTransaction.commit();
    assert(vertexResult.committed);
    assert(!vertexResult.changes.vertices.empty());

    pmxer::PickingTable picking;
    const pmxer::SelectionItem item{pmxer::SelectionKind::vertex, handle.id, handle.generation};
    const auto id = picking.assign(item);
    assert(picking.resolve(id).value() == item);

    const auto path = std::filesystem::temp_directory_path() / "pmxer-editor-test.pmx";
    session.path = path;
    const auto saved = pmxer::saveDocument(session);
    assert(saved.success);
    const auto reloaded = mmd::pmx::load(path);
    assert(mmd::pmx::semanticEqual(session.document.model(), reloaded, mmd::PmxComparisonProfile::preservation));
    std::filesystem::remove(path);
    return 0;
}
