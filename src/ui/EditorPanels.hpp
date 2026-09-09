#pragma once

#include "../editor/WorkspacePolicy.hpp"
#include "../editor/ViewportLighting.hpp"

#include <array>
#include <string>

namespace pmxer {

struct DocumentSession;
class FileDialog;
class GpuModelRenderer;

struct WorkspaceUiState {
    EditorWorkspace active{EditorWorkspace::model};
    bool showDocuments{true};
    bool showViewport{true};
    bool showOutliner{true};
    bool showInspector{true};
    bool showModel{};
    bool showVertex{};
    bool showMaterial{};
    bool showTexture{};
    bool showBone{};
    bool showMorph{};
    bool showDisplayFrame{};
    bool showPhysics{};
    bool showDiagnostics{};
    bool showReferences{};
    bool showDiff{};
    bool resetLayout{};
    bool requestCloseDocument{};
    std::string status;
    ViewportLightingSettings viewportLighting{};
    std::array<WorkspaceViewportProfile, 5> viewportProfiles{
        defaultViewportProfile(EditorWorkspace::model),
        defaultViewportProfile(EditorWorkspace::rig),
        defaultViewportProfile(EditorWorkspace::morph),
        defaultViewportProfile(EditorWorkspace::physics),
        defaultViewportProfile(EditorWorkspace::inspect),
    };

    void resetPanels() noexcept {
        showDocuments = showViewport = showOutliner = showInspector = true;
        showModel = showVertex = showMaterial = showTexture = showBone = false;
        showMorph = showDisplayFrame = showPhysics = showDiagnostics = false;
        showReferences = showDiff = false;
    }
};

void drawMainMenu(DocumentSession *session, FileDialog &fileDialog,
                  WorkspaceUiState &workspace);

void drawEditorPanels(DocumentSession &session, FileDialog &fileDialog,
                      GpuModelRenderer *renderer,
                      WorkspaceUiState &workspace);

} // namespace pmxer
