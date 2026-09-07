#pragma once

namespace pmxer {

class DocumentSession;
class FileDialog;

struct WorkspaceUiState {
    bool showDocuments{true};
    bool showViewport{true};
    bool showModel{true};
    bool showVertex{true};
    bool showMaterial{true};
    bool showTexture{true};
    bool showBone{true};
    bool showMorph{true};
    bool showDisplayFrame{true};
    bool showPhysics{true};
    bool showDiagnostics{true};
    bool showReferences{true};
    bool showDiff{true};
    bool resetLayout{};

    void showAll() noexcept {
        showDocuments = showViewport = showModel = showVertex = showMaterial = showTexture = showBone = true;
        showMorph = showDisplayFrame = showPhysics = showDiagnostics = true;
        showReferences = showDiff = true;
    }
};

void drawEditorPanels(DocumentSession &session, FileDialog &fileDialog, WorkspaceUiState &workspace);

} // namespace pmxer
