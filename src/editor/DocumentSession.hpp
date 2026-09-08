#pragma once

#include "CommandStack.hpp"
#include "Selection.hpp"
#include "ViewportPickCache.hpp"

#include <mmd/document.hpp>
#include <mmd/animation.hpp>

#include <filesystem>
#include <atomic>
#include <chrono>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace pmxer {

inline std::string makeRecoveryId() {
    static std::atomic<std::uint64_t> sequence{1};
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

class PreviewController;
class UiAutomationRegistry;

struct MorphOffsetTargetState {
    bool picking{};
    bool adding{};
    std::size_t offsetIndex{};
    mmd::MorphHandle morph{};
    SelectionKind expectedKind{SelectionKind::vertex};
    std::optional<SelectionItem> target;
};

struct EditorUiState {
    std::size_t vertexIndex{};
    std::size_t materialIndex{};
    std::size_t textureIndex{};
    std::size_t boneIndex{};
    std::size_t boneIkLinkIndex{};
    std::size_t morphIndex{};
    std::size_t morphOffsetIndex{};
    std::size_t displayFrameIndex{};
    std::size_t displayItemIndex{};
    std::size_t rigidBodyIndex{};
    std::size_t jointIndex{};
    std::size_t softBodyIndex{};
    std::optional<mmd::PmxVertex> vertexDraft;
    std::optional<mmd::PmxTexture> textureDraft;
    std::optional<mmd::PmxMetadata> metadataDraft;
    std::optional<mmd::PmxMaterial> materialDraft;
    std::optional<mmd::PmxBone> boneDraft;
    std::optional<mmd::PmxMorph> morphDraft;
    std::optional<mmd::PmxDisplayFrame> displayFrameDraft;
    std::optional<mmd::PmxRigidBody> rigidBodyDraft;
    std::optional<mmd::PmxJoint> jointDraft;
    std::optional<mmd::PmxSoftBody> softBodyDraft;
    std::string openPath;
    std::string pendingOpenPath;
    std::string mergePath;
    std::string motionPath;
    std::string posePath;
    std::string status;
    bool previewPlaying{};
    bool morphOffsetDirty{};
    float cameraYaw{};
    float cameraPitch{};
    float cameraDistance{3.0F};
    mmd::Float3 cameraTarget{};
    bool cameraInitialized{};
    std::uint64_t viewportBoundsRevision{std::numeric_limits<std::uint64_t>::max()};
    mmd::Float3 viewportBoundsMin{};
    mmd::Float3 viewportBoundsMax{};
    float viewportX{};
    float viewportY{};
    float viewportWidth{};
    float viewportHeight{};
    bool viewportVisible{};
    ViewportSelectionMode selectionMode{ViewportSelectionMode::material};
    ViewportTool viewportTool{ViewportTool::select};
    bool localTransform{};
    bool snapTransform{};
    bool xray{};
    bool showGrid{true};
    bool showBones{true};
    bool showPhysics{};
    bool orthographic{};
    bool cameraOrbiting{};
    bool cameraOrbitMoved{};
    bool cameraPanning{};
    bool cameraDollying{};
    float cameraNavigationStartX{};
    float cameraNavigationStartY{};
    bool gizmoDragging{};
    bool boxSelecting{};
    float boxSelectStartX{};
    float boxSelectStartY{};
    float boxSelectEndX{};
    float boxSelectEndY{};
    SelectionItem gizmoSelection{};
    std::array<float, 16> gizmoMatrix{};
    std::optional<SelectionItem> viewportHover;
    MorphOffsetTargetState morphOffsetTarget;
    std::optional<SelectionItem> morphAddTarget;
    std::optional<std::size_t> viewportHoverFace;
    mmd::Float3 viewportHoverPosition{};
    float viewportHoverMouseX{};
    float viewportHoverMouseY{};
    ViewportSelectionMode viewportHoverMode{ViewportSelectionMode::bone};
    std::uint64_t viewportHoverRevision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t viewportHoverFrameRevision{std::numeric_limits<std::uint64_t>::max()};
    std::chrono::steady_clock::time_point viewportHoverUpdated{};
    mutable ViewportPickCache viewportPickCache;
    std::vector<SelectionItem> hiddenMaterials;
    std::vector<SelectionItem> isolatedMaterials;
    std::optional<SelectionItem> automationPendingOutlinerSelection;
    const mmd::AnimatedModelFrame *previewFrame{};

    void clearDrafts() {
        vertexDraft.reset();
        textureDraft.reset();
        metadataDraft.reset();
        materialDraft.reset();
        boneDraft.reset();
        morphDraft.reset();
        morphAddTarget.reset();
        morphOffsetDirty = false;
        displayFrameDraft.reset();
        rigidBodyDraft.reset();
        jointDraft.reset();
        softBodyDraft.reset();
    }
};

struct PreviewSession {
    struct MorphValue {
        SelectionItem selection;
        float weight{};
    };

    std::shared_ptr<PreviewController> controller;
    std::optional<mmd::VmdMotion> motion;
    std::optional<mmd::VpdPose> pose;
    std::optional<mmd::AnimatedModelFrame> frame;
    std::uint64_t revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t frameRevision{};
    double accumulator{};
    std::chrono::steady_clock::time_point lastTick{};
    bool clockInitialized{};
    std::vector<MorphValue> morphValues;
};

struct DerivedEditorState {
    std::uint64_t diagnosticsRevision{std::numeric_limits<std::uint64_t>::max()};
    mmd::ValidationResult diagnostics;
    std::uint64_t diffRevision{std::numeric_limits<std::uint64_t>::max()};
    mmd::SemanticCompareResult diff;
};

struct DocumentSession {
    std::filesystem::path path;
    std::string recoveryId;
    mmd::PmxDocument document;
    CommandStack commands;
    SelectionState selection;
    mmd::ValidationResult validation;
    mmd::PmxChangeSet changes;
    std::optional<mmd::PmxModel> baseline;
    std::optional<mmd::PmxModel> recoveryModel;
    std::optional<std::filesystem::path> recoveryFile;
    bool recoveryPromptOpened{};
    bool modified{};
    bool previewPhysics{true};
    bool previewIk{true};
    std::uint64_t revision{};
    std::uint64_t resourceRevision{};
    std::chrono::steady_clock::time_point lastRecovery{};
    PreviewSession preview;
    DerivedEditorState derived;
    EditorUiState ui;
    UiAutomationRegistry *automation{};

    DocumentSession() : recoveryId(makeRecoveryId()) {}
    explicit DocumentSession(mmd::PmxModel model, std::filesystem::path source = {})
        : path(std::move(source)), recoveryId(makeRecoveryId()), document(std::move(model)), validation(document.validate()),
          baseline(document.model()) {}

    [[nodiscard]] bool undo() {
        if (!commands.undo(document))
            return false;
        modified = commands.isModified();
        selection.retainAlive(document);
        ui.clearDrafts();
        ui.morphOffsetTarget = {};
        ui.gizmoDragging = false;
        ui.gizmoSelection = {};
        ui.viewportHover.reset();
        ui.viewportHoverFace.reset();
        ui.viewportPickCache.clear();
        changes.topologyChanged = true;
        changes.physicsChanged = true;
        changes.texturesChanged = true;
        ++revision;
        validation = document.validate();
        return true;
    }

    [[nodiscard]] bool redo() {
        if (!commands.redo(document))
            return false;
        modified = commands.isModified();
        selection.retainAlive(document);
        ui.clearDrafts();
        ui.morphOffsetTarget = {};
        ui.gizmoDragging = false;
        ui.gizmoSelection = {};
        ui.viewportHover.reset();
        ui.viewportHoverFace.reset();
        ui.viewportPickCache.clear();
        changes.topologyChanged = true;
        changes.physicsChanged = true;
        changes.texturesChanged = true;
        ++revision;
        validation = document.validate();
        return true;
    }
};

} // namespace pmxer
