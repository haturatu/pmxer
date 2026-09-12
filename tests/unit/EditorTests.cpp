#include "../../src/editor/DocumentSession.hpp"
#include "../../src/editor/DeformController.hpp"
#include "../../src/editor/morph/MorphCapture.hpp"
#include "../../src/editor/morph/MorphMask.hpp"
#include "../../src/editor/morph/MorphMixer.hpp"
#include "../../src/editor/morph/MorphOps.hpp"
#include "../../src/editor/EditorSelectionController.hpp"
#include "../../src/editor/EditorSelectionQueries.hpp"
#include "../../src/editor/EditorOperations.hpp"
#include "../../src/editor/RecoveryController.hpp"
#include "../../src/editor/SaveController.hpp"
#include "../../src/editor/PreviewPoseQueries.hpp"
#include "../../src/editor/TransformMath.hpp"
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
#include <mmd/vmd.hpp>

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
    auto mmdLighting = lightingDefaults;
    pmxer::applyViewportShadingPreset(mmdLighting, pmxer::ViewportShadingMode::mmd);
    assert(mmdLighting.lightIntensity == 0.6F);
    assert(mmdLighting.ambientIntensity == 1.0F);
    assert(mmdLighting.exposure == 0.0F);

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

    auto duplicatePreviewModel = sampleModel();
    mmd::PmxMorph firstDuplicate;
    firstDuplicate.name = "same name";
    firstDuplicate.type = 1U;
    firstDuplicate.offsets.push_back(previewOffset);
    duplicatePreviewModel.morphs.push_back(firstDuplicate);
    auto secondDuplicate = firstDuplicate;
    secondDuplicate.offsets.front().vector3 = {4.0F, 0.0F, 0.0F};
    duplicatePreviewModel.morphs.push_back(secondDuplicate);
    mmd::PmxDocument duplicatePreviewDocument(std::move(duplicatePreviewModel));
    pmxer::PreviewController duplicatePreview(duplicatePreviewDocument);
    duplicatePreview.setMorphPreview(duplicatePreviewDocument.morphHandle(1), 0.5F);
    assert(duplicatePreview.evaluate().vertices[0].position[0] == 2.0F);

    auto overrideModel = sampleModel();
    mmd::PmxMorph overrideMorph;
    overrideMorph.name = "override";
    overrideMorph.type = 1U;
    overrideMorph.offsets.push_back(previewOffset);
    overrideModel.morphs.push_back(overrideMorph);
    mmd::VmdMotion overrideMotion;
    overrideMotion.morphs.push_back({"override", 0U, 1.0F});
    pmxer::PreviewController overridePreview(overrideModel);
    overridePreview.setMotion(&overrideMotion);
    overridePreview.setMorphPreview("override", 0.25F);
    assert(std::abs(overridePreview.evaluate().vertices[0].position[0] - 0.5F) < 1e-6F);
    pmxer::PreviewController temporaryVertexPreview(overrideModel);
    temporaryVertexPreview.setVertexPreview({previewOffset});
    assert(std::abs(temporaryVertexPreview.evaluate().vertices[0].position[0] - 2.0F) < 1e-6F);

    auto materialModel = sampleModel();
    materialModel.materials[0].diffuse = {0.2F, 0.3F, 0.4F, 1.0F};
    materialModel.materials[0].specular = {0.1F, 0.2F, 0.3F};
    materialModel.materials[0].shininess = 2.0F;
    materialModel.materials[0].ambient = {0.3F, 0.4F, 0.5F};
    materialModel.materials[0].edgeColor = {0.4F, 0.5F, 0.6F, 1.0F};
    materialModel.materials[0].edgeSize = 1.0F;
    mmd::PmxMorph materialMorph;
    materialMorph.name = "material";
    materialMorph.type = 8U;
    mmd::PmxMorphOffset materialOffset;
    materialOffset.index = 0;
    materialOffset.operation = 1U;
    materialOffset.materialVectors[0] = {0.1F, 0.2F, 0.3F, 0.4F};
    materialOffset.materialVectors[1] = {0.2F, 0.3F, 0.4F, 0.5F};
    materialOffset.materialVectors[2] = {0.3F, 0.4F, 0.5F, 0.6F};
    materialOffset.materialVectors[3] = {0.4F, 0.5F, 0.6F, 0.7F};
    materialOffset.materialVectors[4] = {0.5F, 0.6F, 0.7F, 0.8F};
    materialOffset.materialVectors[5] = {0.6F, 0.7F, 0.8F, 0.9F};
    materialOffset.materialVectors[6] = {0.7F, 0.8F, 0.9F, 1.0F};
    materialMorph.offsets.push_back(materialOffset);
    materialModel.morphs.push_back(materialMorph);
    mmd::PmxDocument materialDocument(std::move(materialModel));
    pmxer::PreviewController materialPreview(materialDocument);
    materialPreview.setMorphPreview(materialDocument.morphHandle(0), 1.0F);
    const auto materialFrame = materialPreview.evaluate();
    assert(std::abs(materialFrame.materials[0].diffuse[0] - 0.3F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].specular[2] - 0.7F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].shininess - 2.5F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].ambient[0] - 0.6F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].edgeColor[2] - 1.2F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].edgeSize - 1.6F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].textureAdd[1] - 0.6F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].sphereAdd[2] - 0.8F) < 1e-6F);
    assert(std::abs(materialFrame.materials[0].toonAdd[3] - 1.0F) < 1e-6F);

    auto mixerModel = sampleModel();
    mixerModel.morphs.push_back(previewMorph);
    pmxer::DocumentSession mixerSession(std::move(mixerModel));
    mixerSession.deform.mode = pmxer::DeformMode::inactive;
    mixerSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::vertex, mixerSession.document.vertexHandle(0).domain,
        mixerSession.document.vertexHandle(0).id, mixerSession.document.vertexHandle(0).generation});
    pmxer::morph::setBlend(mixerSession, mixerSession.document.morphHandle(0), 0.5F);
    assert(mixerSession.deform.mode == pmxer::DeformMode::inactive);
    const auto normalCapabilities = pmxer::transformCapabilities(mixerSession);
    assert(normalCapabilities.move);
    assert(!normalCapabilities.rotate);
    assert(!normalCapabilities.scale);
    mixerSession.deform.mode = pmxer::DeformMode::shape;
    mixerSession.deform.engaged = true;
    mixerSession.deform.suspended = true;
    assert(!mixerSession.deform.active());
    const auto suspendedCapabilities = pmxer::transformCapabilities(mixerSession);
    assert(suspendedCapabilities.move);
    assert(!suspendedCapabilities.rotate);
    assert(!suspendedCapabilities.scale);

    pmxer::DocumentSession bonePreviewSession(sampleModel());
    bonePreviewSession.deform.bones.push_back(
        {bonePreviewSession.document.boneHandle(0), {1.0F, 0.0F, 0.0F},
         {0.0F, 0.0F, 0.0F, 1.0F}});
    pmxer::PreviewController bonePreview(bonePreviewSession.document);
    bonePreview.setBonePreview(pmxer::boneMorphOffsets(bonePreviewSession));
    assert(std::abs(bonePreview.evaluate().vertices[0].position[0] - 1.0F) < 1e-6F);

    pmxer::DocumentSession orderedPreviewSession(sampleModel());
    orderedPreviewSession.deform.vertices.push_back(
        {orderedPreviewSession.document.vertexHandle(0), {0.0F, 1.0F, 0.0F}});
    const auto orderedBone = orderedPreviewSession.document.boneHandle(0);
    constexpr auto pi = 3.14159265358979323846F;
    orderedPreviewSession.deform.bones.push_back(
        {orderedBone, {}, {0.0F, 0.0F, std::sin(0.25F * pi), std::cos(0.25F * pi)}});
    pmxer::PreviewController orderedPreview(orderedPreviewSession.document);
    orderedPreview.setVertexPreview(pmxer::vertexMorphOffsets(orderedPreviewSession));
    orderedPreview.setBonePreview(pmxer::boneMorphOffsets(orderedPreviewSession));
    const auto orderedFrame = orderedPreview.evaluate();
    assert(std::abs(orderedFrame.vertices[0].position[0] + 1.0F) < 1e-5F);
    assert(std::abs(orderedFrame.vertices[0].position[1]) < 1e-5F);

    auto posedBoneModel = sampleModel();
    mmd::PmxBone childBone;
    childBone.name = "child";
    childBone.parent = 0;
    childBone.position = {1.0F, 0.0F, 0.0F};
    posedBoneModel.bones.push_back(childBone);
    pmxer::DocumentSession posedBoneSession(std::move(posedBoneModel));
    mmd::AnimatedModelFrame posedFrame;
    posedFrame.bones.resize(2U);
    posedFrame.bones[0].rotation = {0.0F, 0.0F, std::sin(0.25F * pi),
                                    std::cos(0.25F * pi)};
    posedFrame.bones[1].rotation = posedFrame.bones[0].rotation;
    const auto evaluatedChild = pmxer::evaluatedBonePosition(
        posedBoneSession, posedBoneSession.document.boneHandle(1), &posedFrame);
    assert(std::abs(evaluatedChild[0]) < 1e-5F);
    assert(std::abs(evaluatedChild[1] - 1.0F) < 1e-5F);

    mmd::PmxModel operationModel = sampleModel();
    operationModel.vertices[0].position[0] = -1.0F;
    operationModel.vertices[1].position[0] = 0.0F;
    operationModel.vertices[2].position[0] = 1.0F;
    mmd::PmxMorph operationMorph;
    operationMorph.name = "operation";
    operationMorph.englishName = "operation-en";
    operationMorph.type = 1U;
    operationMorph.panel = 2U;
    for (std::int32_t index = 0; index < 3; ++index) {
        mmd::PmxMorphOffset offset;
        offset.index = index;
        offset.vector3 = {1.0F, 0.0F, 0.0F};
        operationMorph.offsets.push_back(offset);
    }
    operationModel.morphs.push_back(operationMorph);
    mmd::PmxMorph sparseMorph;
    sparseMorph.name = "sparse";
    sparseMorph.type = 1U;
    mmd::PmxMorphOffset sparseOffset;
    sparseOffset.index = 1;
    sparseOffset.vector3 = {4.0F, 0.0F, 0.0F};
    sparseMorph.offsets.push_back(sparseOffset);
    operationModel.morphs.push_back(sparseMorph);
    const auto &operationSource = operationModel.morphs.front();
    const auto scaled = pmxer::morph::scale(pmxer::morph::copy(operationSource), 2.0F);
    assert(scaled.offsets[0].vector3[0] == 2.0F);
    assert(scaled.panel == 2U);
    assert(scaled.englishName == "operation-en");
    const auto inverted = pmxer::morph::invert(pmxer::morph::copy(operationSource));
    assert(inverted.success);
    assert(inverted.data.offsets[0].vector3[0] == -1.0F);
    assert(inverted.data.panel == 2U);
    assert(inverted.data.englishName == "operation-en");

    const auto materialInvert = [](float factor) {
        pmxer::morph::MorphData value;
        value.type = 8U;
        mmd::PmxMorphOffset offset;
        offset.operation = 0U;
        for (std::size_t vector = 0; vector < pmxer::morph::materialMorphVectorCount; ++vector)
            for (auto &component : offset.materialVectors[vector])
                component = factor;
        value.offsets.push_back(offset);
        return value;
    };
    const auto invertedMultiply = pmxer::morph::invert(materialInvert(2.0F));
    assert(invertedMultiply.success);
    assert(std::abs(invertedMultiply.data.offsets[0].materialVectors[0][0] - 0.5F) < 1e-6F);
    for (const auto component : invertedMultiply.data.offsets[0].materialVectors[7])
        assert(component == 0.0F);
    const auto invertedFraction = pmxer::morph::invert(materialInvert(0.5F));
    assert(invertedFraction.success);
    assert(std::abs(invertedFraction.data.offsets[0].materialVectors[0][0] - 2.0F) < 1e-6F);
    const auto scaledMultiply = pmxer::morph::scale(materialInvert(2.0F), 0.5F);
    assert(std::abs(scaledMultiply.offsets[0].materialVectors[0][0] - 1.5F) < 1e-6F);
    for (const auto component : scaledMultiply.offsets[0].materialVectors[7])
        assert(component == 0.0F);
    const auto invertedZero = pmxer::morph::invert(materialInvert(0.0F));
    assert(!invertedZero.success);
    assert(invertedZero.data.offsets.empty());
    assert(invertedZero.message.find("zero factor") != std::string::npos);
    const auto subtractedZero = pmxer::morph::subtract(materialInvert(1.0F), materialInvert(0.0F));
    assert(!subtractedZero.success);
    assert(subtractedZero.data.offsets.empty());
    assert(subtractedZero.message.find("zero factor") != std::string::npos);
    const auto split = pmxer::morph::splitSide(
        operationModel, operationSource, {.0F, 0.1F, false, false});
    assert(split.left.offsets.size() == 2U);
    assert(split.right.offsets.size() == 2U);
    assert(split.left.panel == 2U);
    assert(split.right.panel == 2U);
    assert(split.left.offsets[0].vector3[0] == 1.0F);
    assert(split.right.offsets[1].vector3[0] == 1.0F);
    const auto masked = pmxer::morph::filterByMaterial(
        operationModel, operationSource, 0U,
        pmxer::morph::MaterialFilterMode::excludeUsed);
    assert(masked.offsets.empty());

    pmxer::DocumentSession captureSession(sampleModel());
    const auto captureVertex = captureSession.document.vertexHandle(0);
    captureSession.deform.mode = pmxer::DeformMode::shape;
    captureSession.deform.vertices.push_back({captureVertex, {0.25F, 0.0F, 0.0F}});
    assert(pmxer::morph::captureVertexMorph(captureSession, "captured").success);
    assert(captureSession.selection.items().size() == 1U);
    assert(captureSession.selection.items().front().kind == pmxer::SelectionKind::morph);
    assert(captureSession.ui.pendingOutlinerReveal.has_value());
    assert(captureSession.document.model().morphs.size() == 1U);
    assert(captureSession.document.model().morphs[0].panel == 4U);
    assert(captureSession.document.model().morphs[0].offsets[0].vector3[0] == 0.25F);
    assert(captureSession.commands.undoCount() == 1U);
    captureSession.deform.vertices.push_back({captureVertex, {0.5F, 0.0F, 0.0F}});
    captureSession.deform.bones.push_back(
        {captureSession.document.boneHandle(0), {0.1F, 0.0F, 0.0F},
         {0.0F, 0.0F, 0.0F, 1.0F}});
    assert(pmxer::morph::captureVertexMorph(captureSession, "captured second").success);
    assert(captureSession.deform.vertices.empty());
    assert(captureSession.deform.bones.size() == 1U);
    assert(captureSession.commands.undoCount() == 2U);
    assert(captureSession.undo());
    assert(captureSession.document.model().morphs.size() == 1U);
    assert(captureSession.redo());
    assert(captureSession.document.model().morphs.size() == 2U);

    pmxer::DocumentSession noOpBoneSession(sampleModel());
    noOpBoneSession.deform.bones.push_back(
        {noOpBoneSession.document.boneHandle(0), {},
         {0.0F, 0.0F, 0.0F, 1.0F}});
    assert(!pmxer::morph::captureBoneMorph(noOpBoneSession, "no-op bone").success);
    assert(noOpBoneSession.document.model().morphs.empty());

    auto changedBoneModel = sampleModel();
    pmxer::DocumentSession changedBoneSession(std::move(changedBoneModel));
    const auto changedBone = changedBoneSession.document.boneHandle(0);
    changedBoneSession.deform.bones.push_back(
        {changedBone, {}, {0.0F, 0.0F, std::sin(0.25F * pi), std::cos(0.25F * pi)}});
    changedBoneSession.deform.dirty = true;
    auto changedBoneValue = *changedBoneSession.document.resolve(changedBone);
    changedBoneValue.flags = 0x0100U;
    assert(pmxer::editBone(changedBoneSession, changedBone, changedBoneValue).success);
    const auto changedBoneCapture =
        pmxer::morph::captureBoneMorph(changedBoneSession, "changed bone");
    assert(!changedBoneCapture.success);
    assert(changedBoneCapture.message.find("評価設定が変更") != std::string::npos);
    assert(changedBoneSession.deform.bones.size() == 1U);

    auto changedIkTargetModel = sampleModel();
    mmd::PmxBone changedIkTargetBone;
    changedIkTargetBone.name = "target";
    changedIkTargetModel.bones.push_back(changedIkTargetBone);
    pmxer::DocumentSession changedIkTargetSession(std::move(changedIkTargetModel));
    const auto changedIkBone = changedIkTargetSession.document.boneHandle(0);
    changedIkTargetSession.deform.bones.push_back(
        {changedIkBone, {}, {0.0F, 0.0F, std::sin(0.25F * pi), std::cos(0.25F * pi)}});
    changedIkTargetSession.deform.dirty = true;
    const auto changedIkTargetHandle = changedIkTargetSession.document.boneHandle(1);
    auto changedIkTargetValue = *changedIkTargetSession.document.resolve(changedIkTargetHandle);
    changedIkTargetValue.flags = 0x0020U;
    changedIkTargetValue.ikTarget = 0;
    assert(pmxer::editBone(changedIkTargetSession, changedIkTargetHandle,
                           changedIkTargetValue)
               .success);
    const auto changedIkTargetCapture =
        pmxer::morph::captureBoneMorph(changedIkTargetSession, "changed IK target");
    assert(!changedIkTargetCapture.success);
    assert(changedIkTargetSession.deform.bones.size() == 1U);

    auto changedPhysicsModel = sampleModel();
    pmxer::DocumentSession changedPhysicsSession(std::move(changedPhysicsModel));
    const auto changedPhysicsBone = changedPhysicsSession.document.boneHandle(0);
    changedPhysicsSession.deform.bones.push_back(
        {changedPhysicsBone, {}, {0.0F, 0.0F, std::sin(0.25F * pi), std::cos(0.25F * pi)}});
    changedPhysicsSession.deform.dirty = true;
    mmd::PmxRigidBody changedPhysicsBody;
    changedPhysicsBody.bone = 0;
    changedPhysicsBody.mode = 1U;
    assert(pmxer::applyTransaction(
               changedPhysicsSession,
               [&](auto &transaction) {
                   return static_cast<bool>(transaction.addRigidBody(changedPhysicsBody));
               },
               "attach dynamic rigid body")
               .success);
    const auto changedPhysicsCapture =
        pmxer::morph::captureBoneMorph(changedPhysicsSession, "changed physics");
    assert(!changedPhysicsCapture.success);
    assert(changedPhysicsSession.deform.bones.size() == 1U);

    auto soloModel = sampleModel();
    auto soloFirst = previewMorph;
    soloFirst.name = "blink";
    soloFirst.offsets.front().vector3 = {1.0F, 0.0F, 0.0F};
    soloModel.morphs.push_back(soloFirst);
    auto soloSecond = soloFirst;
    soloSecond.name = "smile";
    soloSecond.offsets.front().vector3 = {2.0F, 0.0F, 0.0F};
    soloModel.morphs.push_back(soloSecond);
    pmxer::DocumentSession soloSession(std::move(soloModel));
    pmxer::morph::setBlend(soloSession, soloSession.document.morphHandle(0), 0.5F);
    pmxer::morph::setBlend(soloSession, soloSession.document.morphHandle(1), 0.8F);
    pmxer::morph::setSoloMorph(soloSession, soloSession.document.morphHandle(1));
    assert(pmxer::morph::effectiveMorphMix(soloSession).size() == 1U);
    assert(pmxer::morph::effectiveMorphMix(soloSession).front().morph ==
           soloSession.document.morphHandle(1));
    assert(pmxer::morph::captureGroupMorph(soloSession, "solo group").success);
    assert(soloSession.document.model().morphs.back().type == 0U);
    assert(soloSession.document.model().morphs.back().offsets.size() == 1U);
    assert(soloSession.document.model().morphs.back().offsets.front().index == 1);
    assert(std::abs(soloSession.document.model().morphs.back().offsets.front().scalar - 0.8F) < 1e-6F);

    auto mixedBakeModel = sampleModel();
    mixedBakeModel.morphs.push_back(soloFirst);
    mmd::PmxMorph bonePart;
    bonePart.name = "bone part";
    bonePart.type = 2U;
    mmd::PmxMorphOffset bonePartOffset;
    bonePartOffset.index = 0;
    bonePartOffset.vector3 = {0.1F, 0.0F, 0.0F};
    bonePartOffset.vector4 = {0.0F, 0.0F, 0.0F, 1.0F};
    bonePart.offsets.push_back(bonePartOffset);
    mixedBakeModel.morphs.push_back(bonePart);
    pmxer::DocumentSession mixedBakeSession(std::move(mixedBakeModel));
    pmxer::morph::setBlend(mixedBakeSession, mixedBakeSession.document.morphHandle(0), 1.0F);
    pmxer::morph::setBlend(mixedBakeSession, mixedBakeSession.document.morphHandle(1), 1.0F);
    const auto bakeAnalysis = pmxer::morph::analyzeVertexMix(mixedBakeSession);
    assert(bakeAnalysis.vertexParts.size() == 1U);
    assert(bakeAnalysis.ignoredTypes.contains(2U));
    assert(!pmxer::morph::bakeMixAsVertexMorph(mixedBakeSession, "mixed bake").success);
    assert(pmxer::morph::bakeMixAsVertexMorph(
               mixedBakeSession, "mixed bake", {.allowIgnoredTypes = true})
               .success);
    assert(mixedBakeSession.document.model().morphs.back().type == 1U);

    pmxer::DocumentSession reverseSession(std::move(operationModel));
    const auto reverseMorph = reverseSession.document.morphHandle(0);
    assert(pmxer::morph::bakeAndReverseBase(reverseSession, reverseMorph).success);
    assert(reverseSession.document.model().vertices[0].position[0] == 0.0F);
    assert(reverseSession.document.model().morphs[0].offsets[0].vector3[0] == -1.0F);
    const auto &rebasedSparse = reverseSession.document.model().morphs[1];
    assert(rebasedSparse.offsets.size() == 3U);
    assert(rebasedSparse.offsets[0].index == 1);
    assert(rebasedSparse.offsets[0].vector3[0] == 3.0F);
    assert(rebasedSparse.offsets[1].index == 0);
    assert(rebasedSparse.offsets[1].vector3[0] == -1.0F);
    assert(reverseSession.commands.undoCount() == 1U);
    assert(reverseSession.undo());
    assert(reverseSession.document.model().vertices[0].position[0] == -1.0F);

    auto referencedReverseModel = sampleModel();
    referencedReverseModel.vertices[0].position[0] = 0.0F;
    mmd::PmxMorph referencedSource;
    referencedSource.name = "referenced source";
    referencedSource.type = 1U;
    mmd::PmxMorphOffset referencedOffset;
    referencedOffset.index = 0;
    referencedOffset.vector3 = {1.0F, 0.0F, 0.0F};
    referencedSource.offsets.push_back(referencedOffset);
    referencedReverseModel.morphs.push_back(referencedSource);
    mmd::PmxMorph referenceGroup;
    referenceGroup.name = "reference group";
    referenceGroup.type = 0U;
    mmd::PmxMorphOffset referenceOffset;
    referenceOffset.index = 0;
    referenceOffset.scalar = 1.0F;
    referenceGroup.offsets.push_back(referenceOffset);
    referencedReverseModel.morphs.push_back(referenceGroup);
    pmxer::DocumentSession referencedReverseSession(std::move(referencedReverseModel));
    const auto referencedSourceHandle = referencedReverseSession.document.morphHandle(0);
    assert(!pmxer::morph::bakeAndReverseBase(referencedReverseSession,
                                              referencedSourceHandle)
                .success);
    assert(referencedReverseSession.document.model().vertices[0].position[0] == 0.0F);
    assert(pmxer::morph::bakeAndReverseBase(
               referencedReverseSession, referencedSourceHandle,
               {.allowReferencedMorph = true})
               .success);
    assert(referencedReverseSession.document.model().vertices[0].position[0] == 1.0F);

    pmxer::DocumentSession pendingTransformSession(sampleModel());
    assert(!pendingTransformSession.hasUnsavedWork());
    pendingTransformSession.deform.vertices.push_back(
        {pendingTransformSession.document.vertexHandle(0), {0.1F, 0.0F, 0.0F}});
    pendingTransformSession.deform.dirty = true;
    assert(pendingTransformSession.hasPendingTransformEdit());
    assert(pendingTransformSession.hasUnsavedWork());
    pendingTransformSession.deform.clearVertexOverlay();
    assert(!pendingTransformSession.hasPendingTransformEdit());
    pendingTransformSession.modified = true;
    assert(pendingTransformSession.hasUnsavedWork());
    pmxer::DocumentSession pendingSaveSession(sampleModel());
    pendingSaveSession.path = std::filesystem::temp_directory_path() / "pmxer-pending-transform-save.pmx";
    pendingSaveSession.deform.vertices.push_back(
        {pendingSaveSession.document.vertexHandle(0), {0.2F, 0.0F, 0.0F}});
    pendingSaveSession.deform.dirty = true;
    assert(!pmxer::saveDocument(pendingSaveSession).success);
    assert(pendingSaveSession.hasPendingTransformEdit());
    assert(pmxer::saveDocument(
               pendingSaveSession, {}, {.allowPendingTransformEdit = true})
               .success);
    assert(pendingSaveSession.hasPendingTransformEdit());
    pmxer::discardPendingTransformEdit(pendingSaveSession);
    assert(!pendingSaveSession.hasPendingTransformEdit());
    std::error_code pendingSaveError;
    std::filesystem::remove(pendingSaveSession.path, pendingSaveError);

    pmxer::DocumentSession transformTabSession(sampleModel());
    auto transformWorkspace = pmxer::EditorWorkspace::model;
    pmxer::activateTransformTab(transformTabSession, transformWorkspace,
                                pmxer::TransformViewTab::vertex);
    assert(transformWorkspace == pmxer::EditorWorkspace::rig);
    assert(transformTabSession.ui.selectionMode == pmxer::ViewportSelectionMode::vertex);
    assert(transformTabSession.ui.viewportTool == pmxer::ViewportTool::select);
    assert(transformTabSession.deform.mode == pmxer::DeformMode::shape);
    pmxer::activateTransformTab(transformTabSession, transformWorkspace,
                                pmxer::TransformViewTab::bone);
    assert(transformWorkspace == pmxer::EditorWorkspace::rig);
    assert(transformTabSession.ui.selectionMode == pmxer::ViewportSelectionMode::bone);
    assert(transformTabSession.ui.showBones);
    assert(transformTabSession.deform.mode == pmxer::DeformMode::pose);
    transformTabSession.deform.tabActivated = false;
    transformWorkspace = pmxer::EditorWorkspace::model;
    pmxer::activateTransformTab(transformTabSession, transformWorkspace,
                                pmxer::TransformViewTab::vertex);
    assert(transformWorkspace == pmxer::EditorWorkspace::rig);
    assert(transformTabSession.ui.selectionMode == pmxer::ViewportSelectionMode::vertex);
    assert(transformTabSession.deform.tabActivated);
    pmxer::activateTransformTab(transformTabSession, transformWorkspace,
                                pmxer::TransformViewTab::morph);
    assert(transformWorkspace == pmxer::EditorWorkspace::morph);
    assert(transformTabSession.deform.mode == pmxer::DeformMode::inactive);

    pmxer::DocumentSession scopedDiscardSession(sampleModel());
    scopedDiscardSession.deform.vertices.push_back(
        {scopedDiscardSession.document.vertexHandle(0), {0.1F, 0.0F, 0.0F}});
    scopedDiscardSession.deform.bones.push_back(
        {scopedDiscardSession.document.boneHandle(0), {0.1F, 0.0F, 0.0F},
         {0.0F, 0.0F, 0.0F, 1.0F}});
    scopedDiscardSession.deform.dirty = true;
    pmxer::discardPendingVertexEdit(scopedDiscardSession);
    assert(scopedDiscardSession.deform.vertices.empty());
    assert(scopedDiscardSession.deform.bones.size() == 1U);
    assert(scopedDiscardSession.deform.dirty);
    pmxer::discardPendingBoneEdit(scopedDiscardSession);
    assert(scopedDiscardSession.deform.bones.empty());
    assert(!scopedDiscardSession.deform.dirty);

    auto symmetryModel = sampleModel();
    symmetryModel.vertices[0].position[0] = -1.0F;
    symmetryModel.vertices[1].position[0] = 0.0F;
    symmetryModel.vertices[2].position[0] = 1.0F;
    pmxer::DocumentSession symmetrySession(std::move(symmetryModel));
    symmetrySession.deform.mode = pmxer::DeformMode::shape;
    symmetrySession.deform.engaged = true;
    symmetrySession.deform.symmetryX = true;
    symmetrySession.deform.symmetryTolerance = 0.01F;
    symmetrySession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::vertex,
        symmetrySession.document.vertexHandle(0).domain,
        symmetrySession.document.vertexHandle(0).id,
        symmetrySession.document.vertexHandle(0).generation});
    std::array<float, 16> symmetryStart{};
    symmetryStart[0] = symmetryStart[5] = symmetryStart[10] = symmetryStart[15] = 1.0F;
    pmxer::beginDeformGizmoDrag(symmetrySession, symmetryStart);
    std::array<float, 16> symmetryDelta{};
    symmetryDelta[0] = symmetryDelta[5] = symmetryDelta[10] = symmetryDelta[15] = 1.0F;
    symmetryDelta[12] = 0.25F;
    pmxer::updateDeformGizmoDrag(symmetrySession, symmetryDelta);
    const auto leftDelta = std::find_if(
        symmetrySession.deform.vertices.begin(), symmetrySession.deform.vertices.end(),
        [&](const auto &delta) { return delta.vertex == symmetrySession.document.vertexHandle(0); });
    const auto rightDelta = std::find_if(
        symmetrySession.deform.vertices.begin(), symmetrySession.deform.vertices.end(),
        [&](const auto &delta) { return delta.vertex == symmetrySession.document.vertexHandle(2); });
    assert(leftDelta != symmetrySession.deform.vertices.end());
    assert(rightDelta != symmetrySession.deform.vertices.end());
    assert(leftDelta->offset[0] == 0.25F);
    assert(rightDelta->offset[0] == -0.25F);

    auto mirrorGroupModel = sampleModel();
    mirrorGroupModel.vertices[0].position = {-1.0F, 0.0F, 0.0F};
    mirrorGroupModel.vertices[1].position = {-1.0F, 0.0F, 0.0F};
    mirrorGroupModel.vertices[2].position = {1.0F, 0.0F, 0.0F};
    mirrorGroupModel.vertices.push_back(mirrorGroupModel.vertices[2]);
    auto distinctMirrorTarget = mirrorGroupModel.vertices[2];
    distinctMirrorTarget.position[0] = 1.015F;
    mirrorGroupModel.vertices.push_back(distinctMirrorTarget);
    pmxer::DocumentSession mirrorGroupSession(std::move(mirrorGroupModel));
    mirrorGroupSession.deform.mode = pmxer::DeformMode::shape;
    mirrorGroupSession.deform.engaged = true;
    mirrorGroupSession.deform.symmetryX = true;
    mirrorGroupSession.deform.symmetryTolerance = 0.02F;
    const auto makeVertexItem = [&](std::size_t index) {
        const auto handle = mirrorGroupSession.document.vertexHandle(index);
        return pmxer::SelectionItem{pmxer::SelectionKind::vertex, handle.domain,
                                    handle.id, handle.generation};
    };
    mirrorGroupSession.selection.set(std::vector<pmxer::SelectionItem>{
        makeVertexItem(0), makeVertexItem(1), makeVertexItem(2)});
    pmxer::beginDeformGizmoDrag(mirrorGroupSession, symmetryStart);
    pmxer::updateDeformGizmoDrag(mirrorGroupSession, symmetryDelta);
    for (std::size_t index = 0; index < 2U; ++index) {
        const auto found = std::find_if(
            mirrorGroupSession.deform.vertices.begin(),
            mirrorGroupSession.deform.vertices.end(), [&](const auto &delta) {
                return delta.vertex == mirrorGroupSession.document.vertexHandle(index);
            });
        assert(found != mirrorGroupSession.deform.vertices.end());
        assert(std::abs(found->offset[0] - 0.25F) < 1e-6F);
    }
    for (std::size_t index = 2U; index < 4U; ++index) {
        const auto found = std::find_if(
            mirrorGroupSession.deform.vertices.begin(),
            mirrorGroupSession.deform.vertices.end(), [&](const auto &delta) {
                return delta.vertex == mirrorGroupSession.document.vertexHandle(index);
            });
        assert(found != mirrorGroupSession.deform.vertices.end());
        assert(std::abs(found->offset[0] + 0.25F) < 1e-6F);
    }
    const auto distinctMirrorDelta = std::find_if(
        mirrorGroupSession.deform.vertices.begin(),
        mirrorGroupSession.deform.vertices.end(), [&](const auto &delta) {
            return delta.vertex == mirrorGroupSession.document.vertexHandle(4);
        });
    assert(distinctMirrorDelta == mirrorGroupSession.deform.vertices.end());

    auto dragModel = sampleModel();
    dragModel.vertices[1].position[0] = 1.0F;
    pmxer::DocumentSession dragSession(std::move(dragModel));
    dragSession.deform.mode = pmxer::DeformMode::shape;
    dragSession.deform.engaged = true;
    const auto dragVertex = dragSession.document.vertexHandle(1);
    dragSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::vertex, dragVertex.domain, dragVertex.id, dragVertex.generation});
    std::array<float, 16> dragStart{};
    dragStart[0] = dragStart[5] = dragStart[10] = dragStart[15] = 1.0F;
    pmxer::beginDeformGizmoDrag(dragSession, dragStart);
    auto dragCurrent = dragStart;
    dragCurrent[12] = 0.1F;
    pmxer::updateDeformGizmoDrag(dragSession, dragCurrent);
    dragCurrent[12] = 0.5F;
    pmxer::updateDeformGizmoDrag(dragSession, dragCurrent);
    assert(std::abs(dragSession.deform.vertices.front().offset[0] - 0.5F) < 1e-6F);

    auto quaternionMatrix = [](const mmd::Float4 &rotation) {
        const auto x = rotation[0];
        const auto y = rotation[1];
        const auto z = rotation[2];
        const auto w = rotation[3];
        std::array<float, 16> matrix{};
        matrix[0] = 1.0F - 2.0F * (y * y + z * z);
        matrix[4] = 2.0F * (x * y - z * w);
        matrix[8] = 2.0F * (x * z + y * w);
        matrix[1] = 2.0F * (x * y + z * w);
        matrix[5] = 1.0F - 2.0F * (x * x + z * z);
        matrix[9] = 2.0F * (y * z - x * w);
        matrix[2] = 2.0F * (x * z - y * w);
        matrix[6] = 2.0F * (y * z + x * w);
        matrix[10] = 1.0F - 2.0F * (x * x + y * y);
        matrix[15] = 1.0F;
        return matrix;
    };
    auto rotationMatrix = [&](float angle) {
        return quaternionMatrix({0.0F, 0.0F, std::sin(angle * 0.5F),
                                  std::cos(angle * 0.5F)});
    };
    auto multiplyMatrices = [](const std::array<float, 16> &lhs,
                               const std::array<float, 16> &rhs) {
        std::array<float, 16> result{};
        for (std::size_t column = 0; column < 4U; ++column)
            for (std::size_t row = 0; row < 4U; ++row)
                for (std::size_t component = 0; component < 4U; ++component)
                    result[column * 4U + row] +=
                        lhs[component * 4U + row] * rhs[column * 4U + component];
        return result;
    };
    pmxer::DocumentSession boneDragSession(sampleModel());
    boneDragSession.deform.mode = pmxer::DeformMode::pose;
    boneDragSession.deform.engaged = true;
    const auto dragBone = boneDragSession.document.boneHandle(0);
    boneDragSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, dragBone.domain, dragBone.id, dragBone.generation});
    const auto identityMatrix = rotationMatrix(0.0F);
    const auto thirtyDegrees = rotationMatrix(pi / 6.0F);
    const auto fiftyDegrees = rotationMatrix(5.0F * pi / 18.0F);
    pmxer::beginDeformGizmoDrag(boneDragSession, identityMatrix);
    pmxer::updateDeformGizmoDrag(boneDragSession, thirtyDegrees);
    mmd::AnimatedModelFrame boneDragFrame;
    boneDragFrame.bones.resize(1U);
    boneDragFrame.bones[0].rotation = {0.0F, 0.0F, std::sin(pi / 12.0F),
                                       std::cos(pi / 12.0F)};
    boneDragSession.ui.previewFrame = &boneDragFrame;
    pmxer::beginDeformGizmoDrag(boneDragSession, thirtyDegrees);
    pmxer::updateDeformGizmoDrag(boneDragSession, fiftyDegrees);
    assert(std::abs(boneDragSession.deform.bones.front().rotation[2] - std::sin(5.0F * pi / 36.0F)) < 1e-5F);
    assert(std::abs(boneDragSession.deform.bones.front().rotation[3] - std::cos(5.0F * pi / 36.0F)) < 1e-5F);

    pmxer::DocumentSession vmdRotationSession(sampleModel());
    mmd::AnimatedModelFrame vmdRotationFrame;
    vmdRotationFrame.bones.resize(1U);
    vmdRotationFrame.bones[0].rotation = {0.0F, 0.0F, std::sin(0.25F * pi),
                                          std::cos(0.25F * pi)};
    vmdRotationSession.ui.previewFrame = &vmdRotationFrame;
    vmdRotationSession.deform.mode = pmxer::DeformMode::pose;
    vmdRotationSession.deform.engaged = true;
    const auto vmdRotationBone = vmdRotationSession.document.boneHandle(0);
    vmdRotationSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, vmdRotationBone.domain, vmdRotationBone.id,
        vmdRotationBone.generation});
    const auto zNinety = rotationMatrix(0.5F * pi);
    const auto xNinety = quaternionMatrix({std::sin(0.25F * pi), 0.0F, 0.0F,
                                           std::cos(0.25F * pi)});
    pmxer::beginDeformGizmoDrag(vmdRotationSession, zNinety);
    pmxer::updateDeformGizmoDrag(
        vmdRotationSession, multiplyMatrices(xNinety, zNinety));
    assert(vmdRotationSession.deform.bones.size() == 1U);
    assert(std::abs(vmdRotationSession.deform.bones.front().rotation[0]) < 1e-5F);
    assert(std::abs(vmdRotationSession.deform.bones.front().rotation[1] + std::sin(0.25F * pi)) < 1e-5F);
    assert(std::abs(vmdRotationSession.deform.bones.front().rotation[2]) < 1e-5F);
    assert(std::abs(vmdRotationSession.deform.bones.front().rotation[3] - std::cos(0.25F * pi)) < 1e-5F);

    pmxer::DocumentSession nonCommutativeSession(sampleModel());
    mmd::AnimatedModelFrame nonCommutativeFrame;
    nonCommutativeFrame.bones.resize(1U);
    const auto xThirty = mmd::Float4{std::sin(pi / 12.0F), 0.0F, 0.0F,
                                     std::cos(pi / 12.0F)};
    nonCommutativeFrame.bones[0].rotation = xThirty;
    nonCommutativeSession.ui.previewFrame = &nonCommutativeFrame;
    nonCommutativeSession.deform.mode = pmxer::DeformMode::pose;
    nonCommutativeSession.deform.engaged = true;
    const auto nonCommutativeBone = nonCommutativeSession.document.boneHandle(0);
    nonCommutativeSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, nonCommutativeBone.domain,
        nonCommutativeBone.id, nonCommutativeBone.generation});
    nonCommutativeSession.deform.bones.push_back({nonCommutativeBone, {}, xThirty});
    nonCommutativeSession.deform.dirty = true;
    const auto yTwenty = mmd::Float4{0.0F, std::sin(pi / 18.0F), 0.0F,
                                     std::cos(pi / 18.0F)};
    const auto expectedNonCommutative = mmd::Float4{
        yTwenty[3] * xThirty[0] + yTwenty[1] * xThirty[2],
        yTwenty[3] * xThirty[1] + yTwenty[1] * xThirty[3],
        -yTwenty[1] * xThirty[0],
        yTwenty[3] * xThirty[3] - yTwenty[1] * xThirty[1]};
    pmxer::beginDeformGizmoDrag(nonCommutativeSession, quaternionMatrix(xThirty));
    pmxer::updateDeformGizmoDrag(
        nonCommutativeSession,
        multiplyMatrices(quaternionMatrix(yTwenty), quaternionMatrix(xThirty)));
    assert(nonCommutativeSession.deform.bones.size() == 1U);
    for (std::size_t component = 0; component < 4U; ++component)
        assert(std::abs(nonCommutativeSession.deform.bones.front().rotation[component] -
                        expectedNonCommutative[component]) < 1e-5F);

    auto childBoneModel = sampleModel();
    mmd::PmxBone posedChild;
    posedChild.name = "posed child";
    posedChild.parent = 0;
    posedChild.position = {1.0F, 0.0F, 0.0F};
    childBoneModel.bones.push_back(posedChild);
    pmxer::DocumentSession childBoneSession(std::move(childBoneModel));
    mmd::AnimatedModelFrame childFrame;
    childFrame.bones.resize(2U);
    const mmd::Float4 parentRotation{0.0F, 0.0F, std::sin(0.25F * pi),
                                     std::cos(0.25F * pi)};
    childFrame.bones[0].rotation = parentRotation;
    childFrame.bones[1].rotation = parentRotation;
    childBoneSession.ui.previewFrame = &childFrame;
    childBoneSession.deform.mode = pmxer::DeformMode::pose;
    childBoneSession.deform.engaged = true;
    const auto childHandle = childBoneSession.document.boneHandle(1);
    childBoneSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, childHandle.domain, childHandle.id,
        childHandle.generation});
    std::array<float, 16> xRotation{};
    xRotation[0] = xRotation[15] = 1.0F;
    xRotation[5] = xRotation[10] = std::cos(0.5F * pi);
    xRotation[6] = std::sin(0.5F * pi);
    xRotation[9] = -std::sin(0.5F * pi);
    const auto childStart = rotationMatrix(0.5F * pi);
    pmxer::beginDeformGizmoDrag(childBoneSession, childStart);
    pmxer::updateDeformGizmoDrag(childBoneSession,
                                 multiplyMatrices(xRotation, childStart));
    assert(childBoneSession.deform.bones.size() == 1U);
    assert(std::abs(childBoneSession.deform.bones.front().rotation[0]) < 1e-5F);
    assert(std::abs(childBoneSession.deform.bones.front().rotation[1] + std::sin(0.25F * pi)) < 1e-5F);
    assert(std::abs(childBoneSession.deform.bones.front().rotation[2]) < 1e-5F);
    assert(std::abs(childBoneSession.deform.bones.front().rotation[3] - std::cos(0.25F * pi)) < 1e-5F);

    // The quaternion composition used for rigid bodies and joints must retain
    // the legacy ImGuizmo X * Y * Z Euler order for compound rotations.
    const auto legacyAxisMatrix = [](std::size_t axis, float angle) {
        std::array<float, 16> matrix{};
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0F;
        const auto sine = std::sin(angle);
        const auto cosine = std::cos(angle);
        if (axis == 0U) {
            matrix[5] = matrix[10] = cosine;
            matrix[6] = sine;
            matrix[9] = -sine;
        } else if (axis == 1U) {
            matrix[0] = matrix[10] = cosine;
            matrix[2] = -sine;
            matrix[8] = sine;
        } else {
            matrix[0] = matrix[5] = cosine;
            matrix[1] = sine;
            matrix[4] = -sine;
        }
        return matrix;
    };
    const auto legacyMultiply = [](const std::array<float, 16> &lhs,
                                   const std::array<float, 16> &rhs) {
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4U; ++row)
            for (std::size_t column = 0; column < 4U; ++column)
                for (std::size_t component = 0; component < 4U; ++component)
                    result[row * 4U + column] +=
                        lhs[row * 4U + component] * rhs[component * 4U + column];
        return result;
    };
    const mmd::Float3 compoundEuler{20.0F * pi / 180.0F,
                                    30.0F * pi / 180.0F,
                                    40.0F * pi / 180.0F};
    const auto legacyCompound = legacyMultiply(
        legacyMultiply(legacyAxisMatrix(0U, compoundEuler[0]),
                       legacyAxisMatrix(1U, compoundEuler[1])),
        legacyAxisMatrix(2U, compoundEuler[2]));
    const auto quaternionCompound = pmxer::composeRotationMatrix(
        pmxer::quaternionFromEuler(compoundEuler));
    for (std::size_t component = 0; component < 16U; ++component)
        assert(std::abs(quaternionCompound[component] - legacyCompound[component]) < 1e-5F);

    auto multiBoneModel = sampleModel();
    mmd::PmxBone childForSelection;
    childForSelection.name = "child";
    childForSelection.parent = 0;
    childForSelection.position = {1.0F, 0.0F, 0.0F};
    multiBoneModel.bones.push_back(childForSelection);
    pmxer::DocumentSession multiBoneSession(std::move(multiBoneModel));
    multiBoneSession.deform.mode = pmxer::DeformMode::pose;
    multiBoneSession.deform.engaged = true;
    const auto multiBoneItem = [&](std::size_t index) {
        const auto handle = multiBoneSession.document.boneHandle(index);
        return pmxer::SelectionItem{pmxer::SelectionKind::bone, handle.domain,
                                    handle.id, handle.generation};
    };
    multiBoneSession.selection.set(std::vector<pmxer::SelectionItem>{
        multiBoneItem(0), multiBoneItem(1)});
    const auto multiBoneCapabilities = pmxer::transformCapabilities(multiBoneSession);
    assert(!multiBoneCapabilities.move);
    assert(!multiBoneCapabilities.rotate);
    assert(!multiBoneCapabilities.scale);
    pmxer::beginDeformGizmoDrag(multiBoneSession, identityMatrix);
    assert(multiBoneSession.deform.dragBones.empty());

    auto appendBoneModel = sampleModel();
    appendBoneModel.bones[0].flags = 0x0100U;
    pmxer::DocumentSession appendBoneSession(std::move(appendBoneModel));
    appendBoneSession.deform.mode = pmxer::DeformMode::pose;
    appendBoneSession.deform.engaged = true;
    const auto appendBone = appendBoneSession.document.boneHandle(0);
    appendBoneSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, appendBone.domain, appendBone.id,
        appendBone.generation});
    assert(!pmxer::transformCapabilities(appendBoneSession).move);

    auto ikBoneModel = sampleModel();
    mmd::PmxBone ikTargetBone;
    ikTargetBone.name = "ik target";
    ikTargetBone.position = {1.0F, 0.0F, 0.0F};
    ikBoneModel.bones.push_back(ikTargetBone);
    ikBoneModel.bones[0].flags = 0x0020U;
    ikBoneModel.bones[0].ikTarget = 1;
    ikBoneModel.bones[0].ikLinks.push_back({1});
    pmxer::DocumentSession ikBoneSession(std::move(ikBoneModel));
    ikBoneSession.deform.mode = pmxer::DeformMode::pose;
    ikBoneSession.deform.engaged = true;
    const auto ikTarget = ikBoneSession.document.boneHandle(1);
    ikBoneSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, ikTarget.domain, ikTarget.id,
        ikTarget.generation});
    assert(!pmxer::transformCapabilities(ikBoneSession).move);

    auto physicsBoneModel = sampleModel();
    mmd::PmxRigidBody physicsBody;
    physicsBody.bone = 0;
    physicsBody.mode = 1U;
    physicsBoneModel.rigidBodies.push_back(physicsBody);
    pmxer::DocumentSession physicsBoneSession(std::move(physicsBoneModel));
    physicsBoneSession.deform.mode = pmxer::DeformMode::pose;
    physicsBoneSession.deform.engaged = true;
    const auto physicsBone = physicsBoneSession.document.boneHandle(0);
    physicsBoneSession.selection.set(pmxer::SelectionItem{
        pmxer::SelectionKind::bone, physicsBone.domain, physicsBone.id,
        physicsBone.generation});
    assert(!pmxer::transformCapabilities(physicsBoneSession).move);

    pmxer::DocumentSession retainedSession(sampleModel());
    const auto retainedVertex = retainedSession.document.vertexHandle(0);
    retainedSession.deform.vertices.push_back({retainedVertex, {0.5F, 0.0F, 0.0F}});
    ++retainedSession.revision;
    retainedSession.retainDeformOverlay();
    assert(retainedSession.deform.vertices.size() == 1U);
    assert(retainedSession.deform.vertices[0].offset[0] == 0.5F);

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
