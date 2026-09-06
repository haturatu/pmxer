#pragma once

#include "../app/CommandLine.hpp"

#include <filesystem>

namespace pmxer {

[[nodiscard]] int runApplication(const EditCommand &options);

} // namespace pmxer
