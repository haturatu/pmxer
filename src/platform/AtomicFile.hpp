#pragma once

#include <filesystem>

namespace pmxer {

[[nodiscard]] std::filesystem::path temporarySibling(const std::filesystem::path &destination);
// Publishes a same-filesystem temporary without replacing an existing entry.
[[nodiscard]] bool atomicCreate(const std::filesystem::path &temporary,
                                const std::filesystem::path &destination);
[[nodiscard]] bool atomicReplace(const std::filesystem::path &temporary,
                                 const std::filesystem::path &destination);

} // namespace pmxer
