#pragma once

#include "CommandStack.hpp"
#include "Selection.hpp"

#include <mmd/document.hpp>
#include <mmd/animation.hpp>

#include <filesystem>
#include <chrono>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace pmxer {

class PreviewController;

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
    std::optional<mmd::PmxMetadata> metadataDraft;
    std::optional<mmd::PmxMaterial> materialDraft;
    std::optional<mmd::PmxBone> boneDraft;
    std::optional<mmd::PmxMorph> morphDraft;
    std::optional<mmd::PmxDisplayFrame> displayFrameDraft;
    std::optional<mmd::PmxRigidBody> rigidBodyDraft;
    std::optional<mmd::PmxJoint> jointDraft;
    std::optional<mmd::PmxSoftBody> softBodyDraft;
    std::string openPath;
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
    float viewportX{};
    float viewportY{};
    float viewportWidth{};
    float viewportHeight{};
    bool viewportVisible{};
    const mmd::AnimatedModelFrame *previewFrame{};

    void clearDrafts() {
        vertexDraft.reset();
        metadataDraft.reset();
        materialDraft.reset();
        boneDraft.reset();
        morphDraft.reset();
        morphOffsetDirty = false;
        displayFrameDraft.reset();
        rigidBodyDraft.reset();
        jointDraft.reset();
        softBodyDraft.reset();
    }
};

struct PreviewSession {
    std::shared_ptr<PreviewController> controller;
    std::optional<mmd::VmdMotion> motion;
    std::optional<mmd::VpdPose> pose;
    std::optional<mmd::AnimatedModelFrame> frame;
    std::uint64_t revision{std::numeric_limits<std::uint64_t>::max()};
    double accumulator{};
    std::chrono::steady_clock::time_point lastTick{};
    bool clockInitialized{};
};

struct DocumentSession {
    std::filesystem::path path;
    mmd::PmxDocument document;
    CommandStack commands;
    SelectionState selection;
    mmd::ValidationResult validation;
    mmd::PmxChangeSet changes;
    std::optional<mmd::PmxModel> baseline;
    bool modified{};
    bool previewPhysics{true};
    bool previewIk{true};
    std::uint64_t revision{};
    std::chrono::steady_clock::time_point lastRecovery{};
    PreviewSession preview;
    EditorUiState ui;

    DocumentSession() = default;
    explicit DocumentSession(mmd::PmxModel model, std::filesystem::path source = {})
        : path(std::move(source)), document(std::move(model)), validation(document.validate()), baseline(document.model()) {}

    [[nodiscard]] bool undo() {
        if (!commands.undo(document))
            return false;
        modified = true;
        selection.clear();
        ui.clearDrafts();
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
        modified = true;
        selection.clear();
        ui.clearDrafts();
        changes.topologyChanged = true;
        changes.physicsChanged = true;
        changes.texturesChanged = true;
        ++revision;
        validation = document.validate();
        return true;
    }
};

} // namespace pmxer
