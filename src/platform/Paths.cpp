#include "Paths.hpp"

#include <cstdlib>

namespace pmxer {
namespace {

std::filesystem::path environmentPath(const char *name) {
    if (const auto *value = std::getenv(name); value != nullptr && *value != '\0')
        return value;
    return {};
}

} // namespace

std::filesystem::path recoveryDirectory() {
#ifdef _WIN32
    auto root = environmentPath("LOCALAPPDATA");
    if (root.empty())
        root = environmentPath("APPDATA");
#elif defined(__APPLE__)
    auto root = environmentPath("HOME") / "Library" / "Application Support";
#else
    auto root = environmentPath("XDG_DATA_HOME");
    if (root.empty())
        root = environmentPath("HOME") / ".local" / "share";
#endif
    return root / "pmxer" / "recovery";
}

std::filesystem::path recoveryPath(const std::filesystem::path &source) {
    const auto name = source.filename().empty() ? "untitled.pmx" : source.filename().string();
    return recoveryDirectory() / (name + ".recovery.pmx");
}

} // namespace pmxer

