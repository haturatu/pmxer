#pragma once

#include "CommandStack.hpp"
#include "Selection.hpp"

#include <mmd/document.hpp>

#include <filesystem>
#include <optional>
#include <utility>

namespace pmxer {

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

    DocumentSession() = default;
    explicit DocumentSession(mmd::PmxModel model, std::filesystem::path source = {})
        : path(std::move(source)), document(std::move(model)), validation(document.validate()), baseline(document.model()) {}

    [[nodiscard]] bool undo() {
        if (!commands.undo(document))
            return false;
        modified = true;
        selection.clear();
        validation = document.validate();
        return true;
    }

    [[nodiscard]] bool redo() {
        if (!commands.redo(document))
            return false;
        modified = true;
        selection.clear();
        validation = document.validate();
        return true;
    }
};

} // namespace pmxer
