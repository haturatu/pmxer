#pragma once

#include "../app/CommandLine.hpp"

namespace pmxer {

[[nodiscard]] int runInfoCommand(const InfoCommand &command);
[[nodiscard]] int runValidateCommand(const ValidateCommand &command);
[[nodiscard]] int runDiffCommand(const DiffCommand &command);
[[nodiscard]] int runNormalizeCommand(const NormalizeCommand &command);

} // namespace pmxer
