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

    auto parentTransaction = session.document.transaction();
    assert(parentTransaction.setBoneParent(boneHandle, std::nullopt));
    const auto parentResult = parentTransaction.commit();
    assert(parentResult.committed);

    auto invalid = sampleModel();
    invalid.metadata.version = 2.0F;
    invalid.vertices[0].weightType = mmd::PmxWeightType::qdef;
    const mmd::PmxDocument invalidDocument(invalid);
    const auto invalidValidation = invalidDocument.validate();
    assert(!invalidValidation.valid());
    assert(!invalidValidation.issues.empty());
    assert(invalidValidation.issues.front().location.kind == mmd::ReferenceObjectKind::vertex);

    auto skin = mmd::PmxVertexSkin{};
    skin.bones[0] = boneHandle;
    auto vertexTransaction = session.document.transaction();
    assert(vertexTransaction.setVertexSkin(handle, skin));
    const auto vertexResult = vertexTransaction.commit();
    assert(vertexResult.committed);
    assert(!vertexResult.changes.vertices.empty());

    auto structure = session.document.transaction();
    mmd::PmxMorph morph;
    morph.name = "vertex_morph";
    morph.type = 1;
    const auto morphHandle = structure.addMorph(morph);
    assert(morphHandle);
    assert(structure.addVertexMorphOffset(morphHandle, handle, {0.1F, 0.0F, 0.0F}));
    const auto frameHandle = structure.addDisplayFrame({});
    assert(frameHandle);
    assert(structure.addDisplayFrameItem(frameHandle, boneHandle));
    mmd::PmxRigidBody bodyA;
    bodyA.name = "body_a";
    mmd::PmxRigidBody bodyB;
    bodyB.name = "body_b";
    const auto bodyHandleA = structure.addRigidBody(bodyA);
    const auto bodyHandleB = structure.addRigidBody(bodyB);
    assert(bodyHandleA && bodyHandleB);
    mmd::PmxJoint joint;
    joint.name = "joint";
    const auto jointHandle = structure.addJoint({joint, bodyHandleA, bodyHandleB});
    assert(jointHandle);
    const auto structureResult = structure.commit();
    assert(structureResult.committed);
    assert(structureResult.changes.topologyChanged);

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
