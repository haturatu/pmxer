#include "ValidationController.hpp"

namespace pmxer {

ValidationSummary summarizeValidation(const mmd::ValidationResult &result) noexcept {
    ValidationSummary summary;
    for (const auto &issue : result.issues) {
        switch (issue.severity) {
        case mmd::ValidationSeverity::info:
            ++summary.info;
            break;
        case mmd::ValidationSeverity::warning:
            ++summary.warnings;
            break;
        case mmd::ValidationSeverity::error:
            ++summary.errors;
            break;
        case mmd::ValidationSeverity::fatal:
            ++summary.fatals;
            break;
        }
    }
    return summary;
}

std::string validationSeverityName(mmd::ValidationSeverity severity) {
    switch (severity) {
    case mmd::ValidationSeverity::info:
        return "INFO";
    case mmd::ValidationSeverity::warning:
        return "WARN";
    case mmd::ValidationSeverity::error:
        return "ERROR";
    case mmd::ValidationSeverity::fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

} // namespace pmxer

