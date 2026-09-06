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

[[nodiscard]] SaveResult saveDocument(DocumentSession &session, const std::filesystem::path &destination = {});

} // namespace pmxer

