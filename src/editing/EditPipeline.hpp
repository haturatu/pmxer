#pragma once
#include "ModelQuery.hpp"
#include "editor/AiEditExecutor.hpp"
#include <stdexcept>
namespace pmxer::editing {
struct PlanError : std::runtime_error {
    std::string code;
    nlohmann::json details;
    PlanError(std::string code, std::string message, nlohmann::json details = {});
};
struct PreparedEdit {
    ai::EditRegion region;
    std::unique_ptr<DocumentSession> preview;
    nlohmann::json report;
};
nlohmann::json editSchema();
PreparedEdit prepareEdit(const DocumentSession &, const nlohmann::json &plan,
                         std::string_view sourceSha256 = {}, std::string_view outputSha256 = {});
} // namespace pmxer::editing
