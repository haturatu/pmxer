#pragma once

#include <array>

namespace pmxer {

class DocumentSession;
class FileDialog;

enum class EditorWorkspace { model, rig, morph, physics, inspect };

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
    std::array<char, 128> search{};

    void resetPanels() noexcept {
        showDocuments = showViewport = showOutliner = showInspector = true;
        showModel = showVertex = showMaterial = showTexture = showBone = false;
        showMorph = showDisplayFrame = showPhysics = showDiagnostics = false;
        showReferences = showDiff = false;
    }
};

void drawEditorPanels(DocumentSession &session, FileDialog &fileDialog, WorkspaceUiState &workspace);

} // namespace pmxer
