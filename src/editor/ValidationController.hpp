#pragma once

#include <mmd/pmx.hpp>

#include <cstddef>
#include <string>

namespace pmxer {

struct ValidationSummary {
    std::size_t info{};
    std::size_t warnings{};
    std::size_t errors{};
    std::size_t fatals{};
};

[[nodiscard]] ValidationSummary summarizeValidation(const mmd::ValidationResult &result) noexcept;
[[nodiscard]] std::string validationSeverityName(mmd::ValidationSeverity severity);

} // namespace pmxer

