#pragma once

#include <mmd/document.hpp>

namespace pmxer {

[[nodiscard]] mmd::ValidationResult validateForEditing(const mmd::PmxDocument &document);

} // namespace pmxer
