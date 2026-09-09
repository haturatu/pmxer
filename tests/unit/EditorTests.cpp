#include "../../src/editor/DocumentSession.hpp"
#include "../../src/editor/EditorSelectionController.hpp"
#include "../../src/editor/EditorSelectionQueries.hpp"
#include "../../src/editor/EditorOperations.hpp"
#include "../../src/editor/RecoveryController.hpp"
#include "../../src/editor/SaveController.hpp"
#include "../../src/editor/ViewportCapabilities.hpp"
#include "../../src/editor/ViewportLighting.hpp"
#include "../../src/editor/WorkspacePolicy.hpp"
#include "../../src/editor/UiStatus.hpp"
#include "../../src/render/Camera.hpp"
#include "../../src/render/Picking.hpp"
#include "../../src/render/PreviewTextureFallbacks.hpp"
#include "../../src/preview/PreviewController.hpp"
#include "../../src/ui/WorkspaceLayout.hpp"

#include <mmd/pmx.hpp>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

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
    const pmxer::ViewportLightingSettings lightingDefaults{};
    assert(lightingDefaults.mode == pmxer::ViewportShadingMode::neutral);
    assert(lightingDefaults.exposure == 0.5F);

    auto previewModel = sampleModel();
    mmd::PmxMorph previewMorph;
    previewMorph.name = "preview";
    previewMorph.type = 1;
    mmd::PmxMorphOffset previewOffset;
    previewOffset.index = 0;
    previewOffset.vector3 = {2.0F, 0.0F, 0.0F};
    previewMorph.offsets.push_back(previewOffset);
    previewModel.morphs.push_back(previewMorph);
    pmxer::PreviewController previewController(previewModel);
    assert(previewController.evaluate().vertices[0].position[0] == 0.0F);
    previewController.setMorphPreview("preview", 0.5F);
    assert(previewController.evaluate().vertices[0].position[0] == 1.0F);
    previewController.clearMorphPreview("preview");
    assert(previewController.evaluate().vertices[0].position[0] == 0.0F);

    const pmxer::CameraState perspective{{0.0F, 0.0F, 0.0F}, 0.0F, 0.0F, 10.0F, false};
    const auto perspectiveCenter =
        pmxer::projectWorldToScreen(perspective, {0.0F, 0.0F, 0.0F}, 0.0F, 0.0F, 800.0F, 600.0F);
    assert(perspectiveCenter.inFront);
    assert(std::abs(perspectiveCenter.x - 400.0F) < 0.001F);
    assert(std::abs(perspectiveCenter.y - 300.0F) < 0.001F);
    assert(!pmxer::projectWorldToScreen(perspective, {0.0F, 0.0F, 20.0F}, 0.0F, 0.0F, 800.0F, 600.0F)
                .inFront);
    auto orthographic = perspective;
    orthographic.orthographic = true;
    const auto orthographicCenter =
        pmxer::projectWorldToScreen(orthographic, {0.0F, 0.0F, 0.0F}, 0.0F, 0.0F, 800.0F, 600.0F);
    assert(orthographicCenter.inFront);
    assert(std::abs(orthographicCenter.x - 400.0F) < 0.001F);
    assert(std::abs(orthographicCenter.y - 300.0F) < 0.001F);

    const auto sharedToon = pmxer::makeSharedToonFallback(0);
    for (const auto channel : sharedToon)
        assert(channel == 255U);

    const auto neutralToon = pmxer::makeNeutralToonFallback();
    assert(neutralToon[0] == 255U);
    assert(neutralToon[63U * 4U] == 96U);
    for (std::size_t row = 1; row < 64U; ++row)
        assert(neutralToon[(row - 1U) * 4U] >= neutralToon[row * 4U]);

    pmxer::DocumentSession recovered(sampleModel());
    recovered.commands.markDirty();
    const auto recoveredHandle = recovered.document.vertexHandle(0);
    const auto recoveredMaterial = recovered.document.materialHandle(0);
    recovered.selection.set({pmxer::SelectionKind::material, recoveredMaterial.domain,
                             recoveredMaterial.id, recoveredMaterial.generation});
    auto recoveredVertex = *recovered.document.resolve(recoveredHandle);
    recoveredVertex.position[0] = 4.0F;
    assert(pmxer::editVertex(recovered, recoveredHandle, recoveredVertex).success);
    assert(recovered.undo());
    assert(recovered.selection.contains({pmxer::SelectionKind::material,
                                         recoveredMaterial.domain, recoveredMaterial.id,
                                         recoveredMaterial.generation}));
    assert(recovered.modified);
    assert(recovered.commands.isModified());
    assert(recovered.redo());
    recovered.commands.markClean();
    assert(recovered.undo());
    assert(recovered.modified);
    assert(recovered.redo());
    assert(!recovered.modified);

    pmxer::DocumentSession session(sampleModel());
    const auto modelPolicy = pmxer::workspacePolicy(pmxer::EditorWorkspace::model);
    assert(modelPolicy.allows(pmxer::SelectionKind::material));
    assert(!modelPolicy.allows(pmxer::SelectionKind::bone));
    assert(pmxer::workspacePolicy(pmxer::EditorWorkspace::physics)
               .allows(pmxer::SelectionKind::softBody));
    assert(pmxer::workspacePolicy(pmxer::EditorWorkspace::inspect)
               .allows(pmxer::SelectionKind::bone));
    assert(pmxer::defaultViewportProfile(pmxer::EditorWorkspace::physics)
               .physicsMode == pmxer::PhysicsOverlayMode::context);
    const auto morphPolicy = pmxer::workspacePolicy(pmxer::EditorWorkspace::morph);
    assert(!morphPolicy.defaultViewportMode.has_value());
    assert(!morphPolicy.allows(pmxer::ViewportSelectionMode::material));
    const auto boneForPolicy = session.document.boneHandle(0);
    const auto materialForPolicy = session.document.materialHandle(0);
    session.selection.set(std::vector<pmxer::SelectionItem>{
        {pmxer::SelectionKind::bone, boneForPolicy.domain, boneForPolicy.id, boneForPolicy.generation},
        {pmxer::SelectionKind::material, materialForPolicy.domain, materialForPolicy.id,
         materialForPolicy.generation}});
    pmxer::applyWorkspacePolicy(session, pmxer::workspacePolicy(pmxer::EditorWorkspace::rig));
    assert(session.selection.items().size() == 1U);
    assert(session.selection.items().front().kind == pmxer::SelectionKind::bone);
    assert(session.ui.showBones);
    assert(!session.ui.showPhysics);
    const auto capabilities = pmxer::transformCapabilities(session);
    assert(capabilities.move);
    assert(!capabilities.rotate);
    assert(!capabilities.scale);
    pmxer::applyWorkspacePolicy(session, pmxer::workspacePolicy(pmxer::EditorWorkspace::physics));
    assert(session.selection.items().empty());
    assert(!session.ui.showBones);
    assert(session.ui.showPhysics);

    pmxer::DocumentSession synchronized(sampleModel());
    auto synchronizedWorkspace = pmxer::EditorWorkspace::morph;
    const auto synchronizedBone = synchronized.document.boneHandle(0);
    pmxer::selectPrimary(
        synchronized, synchronizedWorkspace,
        {pmxer::SelectionKind::bone, synchronizedBone.domain, synchronizedBone.id,
         synchronizedBone.generation},
        pmxer::SelectionOrigin::viewport);
    assert(synchronizedWorkspace == pmxer::EditorWorkspace::rig);
    assert(synchronized.selection.items().front().kind == pmxer::SelectionKind::bone);
    assert(pmxer::actionAvailability(pmxer::EditorAction::viewportRotate,
                                     synchronized)
               .support == pmxer::SupportLevel::unsupported);

    const auto boneItem = pmxer::SelectionItem{
        pmxer::SelectionKind::bone, boneForPolicy.domain, boneForPolicy.id,
        boneForPolicy.generation};
    assert(pmxer::weightedVertices(session, boneItem).size() == 3U);
    const auto materialItem = pmxer::SelectionItem{
        pmxer::SelectionKind::material, materialForPolicy.domain,
        materialForPolicy.id, materialForPolicy.generation};
    assert(pmxer::facesForMaterial(session, materialItem).size() == 1U);
    assert(pmxer::verticesForMaterial(session, materialItem).size() == 3U);

    auto weightedModel = sampleModel();
    for (int index = 1; index < 4; ++index) {
        mmd::PmxBone bone;
        bone.name = "bone" + std::to_string(index);
        weightedModel.bones.push_back(bone);
    }
    weightedModel.vertices[0].weightType = mmd::PmxWeightType::bdef4;
    weightedModel.vertices[0].bones = {0, 1, 2, 3};
    weightedModel.vertices[0].weights = {1.0F, 0.0F, 0.0F, 0.0F};
    pmxer::DocumentSession weightedSession(std::move(weightedModel));
    const auto weightedVertex = weightedSession.document.vertexHandle(0);
    const auto weightedVertexItem = pmxer::SelectionItem{
        pmxer::SelectionKind::vertex, weightedVertex.domain, weightedVertex.id,
        weightedVertex.generation};
    for (int index = 0; index < 4; ++index) {
        const auto bone = weightedSession.document.boneHandle(
            static_cast<std::size_t>(index));
        const auto boneItem = pmxer::SelectionItem{
            pmxer::SelectionKind::bone, bone.domain, bone.id, bone.generation};
        const auto vertices = pmxer::weightedVertices(weightedSession, boneItem);
        const auto found = std::find(vertices.begin(), vertices.end(),
                                     weightedVertexItem) != vertices.end();
        assert(found == (index == 0));
    }

    auto physicsModel = sampleModel();
    physicsModel.rigidBodies.resize(2);
    physicsModel.joints.push_back({});
    physicsModel.joints[0].bodyA = 0;
    physicsModel.joints[0].bodyB = 1;
    pmxer::DocumentSession physicsSession(std::move(physicsModel));
    const auto body = physicsSession.document.rigidBodyHandle(0);
    const auto physicsJoint = physicsSession.document.jointHandle(0);
    physicsSession.selection.set({pmxer::SelectionKind::rigidBody, body.domain,
                                  body.id, body.generation});
    const auto bodyRelations =
        pmxer::physicsSelectionRelations(physicsSession);
    assert(bodyRelations.bodySelected(body.id));
    assert(bodyRelations.jointRelated(physicsJoint.id));
    physicsSession.selection.set({pmxer::SelectionKind::joint,
                                  physicsJoint.domain, physicsJoint.id,
                                  physicsJoint.generation});
    const auto jointRelations =
        pmxer::physicsSelectionRelations(physicsSession);
    assert(jointRelations.jointSelected(physicsJoint.id));
    assert(jointRelations.bodyRelated(body.id));
    pmxer::setStatus(session, "一時通知", pmxer::UiStatusKind::success,
                     std::chrono::milliseconds::zero());
    pmxer::updateStatusLifetime(session);
    assert(session.ui.status.empty());

    auto morphTargetModel = sampleModel();
    mmd::PmxMorph vertexMorph;
    vertexMorph.name = "vertex morph";
    vertexMorph.type = 1;
    mmd::PmxMorphOffset vertexOffset;
    vertexOffset.index = 0;
    vertexMorph.offsets.push_back(vertexOffset);
    morphTargetModel.morphs.push_back(vertexMorph);
    pmxer::DocumentSession morphTargetSession(std::move(morphTargetModel));
    const auto targetMorph = morphTargetSession.document.morphHandle(0);
    morphTargetSession.ui.morphDraft =
        *morphTargetSession.document.resolve(targetMorph);
    morphTargetSession.ui.morphOffsetTarget = {
        true, false, 0, targetMorph, pmxer::SelectionKind::vertex, std::nullopt};
    const auto targetVertex = morphTargetSession.document.vertexHandle(1);
    const pmxer::SelectionItem targetVertexItem{
        pmxer::SelectionKind::vertex, targetVertex.domain, targetVertex.id,
        targetVertex.generation};
    assert(pmxer::selectMorphOffsetTarget(morphTargetSession,
                                          targetVertexItem));
    assert(!morphTargetSession.ui.morphOffsetTarget.picking);
    assert(morphTargetSession.ui.morphDraft->offsets[0].index == 1);
    assert(morphTargetSession.ui.morphOffsetDirty);
    morphTargetSession.ui.morphOffsetTarget = {
        true, true, 0, targetMorph, pmxer::SelectionKind::vertex, std::nullopt};
    assert(pmxer::selectMorphOffsetTarget(morphTargetSession,
                                          targetVertexItem));
    assert(morphTargetSession.ui.morphAddTarget == targetVertexItem);
    auto morphWorkspace = pmxer::EditorWorkspace::morph;
    morphTargetSession.ui.selectionMode = pmxer::ViewportSelectionMode::bone;
    assert(pmxer::selectAllForMode(
        morphTargetSession, morphWorkspace,
        morphTargetSession.ui.selectionMode,
        pmxer::SelectionOrigin::viewport));
    assert(morphTargetSession.selection.items().size() == 1U);
    assert(morphTargetSession.selection.items().front().kind ==
           pmxer::SelectionKind::morph);

    const auto layoutPath = std::filesystem::temp_directory_path() / "pmxer-workspace-layout-test.ini";
    assert(pmxer::saveWorkspaceLayout(layoutPath, "[Window][pmxer]\nPos=0,0\n"));
    pmxer::WorkspaceLayout layout;
    assert(pmxer::loadWorkspaceLayout(layoutPath, layout) ==
           pmxer::WorkspaceLayoutLoadResult::loaded);
    assert(layout.version == pmxer::kWorkspaceLayoutVersion);
    assert(layout.imguiIni.find("[Window]") != std::string::npos);
    {
        std::ofstream legacy(layoutPath, std::ios::trunc);
        legacy << "[Window][legacy]\nPos=0,0\n";
    }
    assert(pmxer::loadWorkspaceLayout(layoutPath, layout) ==
           pmxer::WorkspaceLayoutLoadResult::legacy);
    {
        std::ofstream unsupported(layoutPath, std::ios::trunc);
        unsupported << "PMXER_WORKSPACE_LAYOUT 1\n[Window][old]\n";
    }
    assert(pmxer::loadWorkspaceLayout(layoutPath, layout) ==
           pmxer::WorkspaceLayoutLoadResult::unsupportedVersion);
    {
        std::ofstream corrupt(layoutPath, std::ios::trunc);
        corrupt << "PMXER_WORKSPACE_LAYOUT nope\n[Window][broken]\n";
    }
    assert(pmxer::loadWorkspaceLayout(layoutPath, layout) ==
           pmxer::WorkspaceLayoutLoadResult::corrupt);
    std::filesystem::remove(layoutPath);

    assert(session.document.validate().valid());
    const auto handle = session.document.vertexHandle(0);
    assert(!session.document.referencesTo(session.document.boneHandle(0)).empty());
    pmxer::DocumentSession otherSession(sampleModel());
    assert(session.document.resolve(otherSession.document.vertexHandle(0)) == nullptr);
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

    auto invalidVertex = *session.document.resolve(handle);
    invalidVertex.bones[0] = 99;
    assert(!session.document.replaceVertex(handle, invalidVertex).committed);
    assert(session.document.resolve(handle)->bones[0] == 0);

    pmxer::DocumentSession historySession(sampleModel());
    const auto historyHandle = historySession.document.vertexHandle(0);
    mmd::MorphHandle historyMorph;
    auto historyVertex = *historySession.document.resolve(historyHandle);
    historyVertex.position[0] = 3.0F;
    assert(pmxer::editVertex(historySession, historyHandle, historyVertex).success);
    assert(pmxer::applyTransaction(
               historySession,
               [&historyMorph](auto &transaction) {
                   mmd::PmxMorph morph;
                   morph.name = "history_morph";
                   morph.type = 1;
                   historyMorph = transaction.addMorph(std::move(morph));
                   return static_cast<bool>(historyMorph);
               },
               "構造変更")
               .success);
    assert(historySession.undo());
    const auto restoredHandle = historySession.document.vertexHandle(0);
    assert(historyHandle.domain == restoredHandle.domain);
    assert(historyHandle.id == restoredHandle.id);
    assert(historyHandle.generation == restoredHandle.generation);
    assert(historySession.document.resolve(historyHandle) != nullptr);
    assert(historySession.undo());
    assert(historySession.document.resolve(historyHandle) != nullptr);
    assert(historySession.document.resolve(historyHandle)->position[0] == 0.0F);
    assert(historySession.redo());
    assert(historySession.redo());
    assert(historySession.document.resolve(historyMorph) != nullptr);
    assert(historySession.document.resolve(historyMorph)->name == "history_morph");

    auto reorderModel = sampleModel();
    reorderModel.morphs.resize(2);
    reorderModel.morphs[0].name = "first";
    reorderModel.morphs[1].name = "second";
    pmxer::DocumentSession reorderSession(std::move(reorderModel));
    const auto firstMorph = reorderSession.document.morphHandle(0);
    assert(pmxer::moveMorph(reorderSession, firstMorph, 1).success);
    assert(reorderSession.document.resolve(firstMorph)->name == "first");
    assert(reorderSession.document.morphHandle(1) == firstMorph);
    assert(reorderSession.undo());
    assert(reorderSession.document.morphHandle(0) == firstMorph);
    assert(reorderSession.redo());
    assert(reorderSession.document.morphHandle(1) == firstMorph);

    const auto materialHandle = session.document.materialHandle(0);
    auto material = *session.document.resolve(materialHandle);
    material.diffuse[0] = 0.25F;
    assert(pmxer::editMaterial(session, materialHandle, material).success);
    auto metadata = session.document.model().metadata;
    metadata.modelName = "edited metadata";
    assert(pmxer::editMetadata(session, metadata).success);
    assert(session.commands.undo(session.document));
    assert(session.document.model().metadata.modelName == "sample");
    assert(session.commands.redo(session.document));
    assert(session.document.model().metadata.modelName == "edited metadata");
    const auto boneHandle = session.document.boneHandle(0);
    auto bone = *session.document.resolve(boneHandle);
    bone.name = "edited";
    assert(pmxer::editBone(session, boneHandle, bone).success);
    auto cyclicBone = *session.document.resolve(boneHandle);
    cyclicBone.parent = 0;
    assert(!session.document.replaceBone(boneHandle, cyclicBone).committed);
    assert(session.document.resolve(boneHandle)->parent == -1);

    auto parentTransaction = session.document.transaction();
    assert(parentTransaction.setBoneParent(boneHandle, std::nullopt));
    const auto parentResult = parentTransaction.commit();
    assert(parentResult.committed);

    auto invalid = sampleModel();
    invalid.metadata.version = 2.0F;
    invalid.vertices[2].weightType = mmd::PmxWeightType::qdef;
    const mmd::PmxDocument invalidDocument(invalid);
    const auto invalidValidation = invalidDocument.validate();
    assert(!invalidValidation.valid());
    assert(!invalidValidation.issues.empty());
    assert(invalidValidation.issues.front().location.kind == mmd::ReferenceObjectKind::vertex);
    assert(invalidValidation.issues.front().location.id == invalidDocument.vertexHandle(2).id);

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
    assert(structure.setVertexMorphOffset(morphHandle, 0, handle, {0.2F, 0.0F, 0.0F}));
    assert(structure.moveMorphOffset(morphHandle, 0, 0));
    const auto frameHandle = structure.addDisplayFrame({});
    assert(frameHandle);
    assert(structure.addDisplayFrameItem(frameHandle, boneHandle));
    assert(structure.setDisplayFrameItem(frameHandle, 0, boneHandle));
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
    const pmxer::SelectionItem item{
        pmxer::SelectionKind::vertex, handle.domain, handle.id, handle.generation};
    const auto id = picking.assign(item);
    assert(picking.resolve(id).value() == item);
    const auto restored = pmxer::selectionHandle<mmd::VertexTag>(session.document, item);
    assert(restored == handle);
    assert(session.document.resolve(restored) != nullptr);
    assert(!pmxer::selectionHandle<mmd::VertexTag>(otherSession.document, item));
    const pmxer::SelectionItem secondItem{
        pmxer::SelectionKind::vertex, handle.domain, handle.id + 1U, handle.generation};
    pmxer::SelectionState bulkSelection;
    bulkSelection.set(std::vector<pmxer::SelectionItem>{item, item});
    assert(bulkSelection.items().size() == 1U);
    bulkSelection.add(std::vector<pmxer::SelectionItem>{item, secondItem});
    assert(bulkSelection.items().size() == 2U);
    bulkSelection.toggle(std::vector<pmxer::SelectionItem>{item, secondItem});
    assert(bulkSelection.items().empty());

    const auto path = std::filesystem::temp_directory_path() / "pmxer-editor-test.pmx";
    session.path = path;
    const auto recovery = pmxer::writeRecovery(session);
    assert(recovery.success);
    assert(pmxer::loadRecovery(path).has_value());
    assert(pmxer::discardRecovery(path));
    const auto secondRecovery = pmxer::writeRecovery(session);
    assert(secondRecovery.success);
    const auto resourceRevision = session.resourceRevision;
    const auto saved = pmxer::saveDocument(session);
    assert(saved.success);
    assert(!std::filesystem::exists(secondRecovery.path));
    assert(session.document.model().sourcePath == path);
    assert(session.baseline->sourcePath == path);
    assert(session.resourceRevision == resourceRevision + 1);
    pmxer::DocumentSession untitled(sampleModel());
    untitled.commands.markDirty();
    untitled.modified = true;
    const auto untitledRecovery = pmxer::writeRecovery(untitled);
    assert(untitledRecovery.success);
    const auto untitledDestination = path.parent_path() / "pmxer-untitled-save-test.pmx";
    assert(pmxer::saveDocument(untitled, untitledDestination).success);
    assert(!std::filesystem::exists(untitledRecovery.path));
    std::filesystem::remove(untitledDestination);
    const auto reloaded = mmd::pmx::load(path);
    assert(mmd::pmx::semanticEqual(session.document.model(), reloaded, mmd::PmxComparisonProfile::preservation));
    std::filesystem::remove(path);
    return 0;
}
