#pragma once

#include <mmd/pmx.hpp>

namespace pmxer {

[[nodiscard]] mmd::ValidationResult validateForEditing(const mmd::PmxDocument &document);

} // namespace pmxer
