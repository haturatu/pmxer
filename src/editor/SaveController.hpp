#pragma once

#include "DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <filesystem>
#include <string>

namespace pmxer {

struct SaveResult {
    bool success{};
    std::string message;
    mmd::PmxSaveReport report;
    mmd::SemanticCompareResult verification;
};

struct SaveOptions {
    // Allows saving the document while a temporary Transform View overlay is
    // pending. The caller owns the overlay and must discard it only after a
    // successful save.
    bool allowPendingTransformEdit{};
};

[[nodiscard]] SaveResult saveDocument(
    DocumentSession &session, const std::filesystem::path &destination = {},
    SaveOptions options = {});

} // namespace pmxer
