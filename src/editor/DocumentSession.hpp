#pragma once

#include "CommandStack.hpp"
#include "Selection.hpp"

#include <mmd/document.hpp>

#include <filesystem>
#include <optional>

namespace pmxer {

struct DocumentSession {
    std::filesystem::path path;
    mmd::PmxDocument document;
    CommandStack commands;
    SelectionState selection;
    mmd::ValidationResult validation;
    std::optional<mmd::PmxModel> baseline;
    bool modified{};
    bool previewPhysics{true};
    bool previewIk{true};

    DocumentSession() = default;
    explicit DocumentSession(mmd::PmxModel model, std::filesystem::path source = {})
        : path(std::move(source)), document(std::move(model)), validation(document.validate()), baseline(document.model()) {}
};

} // namespace pmxer

