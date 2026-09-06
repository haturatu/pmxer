#include "Paths.hpp"

#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <sstream>

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
    if (root.empty())
        root = std::filesystem::temp_directory_path();
    return root / "pmxer" / "recovery";
}

std::filesystem::path recoveryPath(const std::filesystem::path &source) {
    return recoveryPath(source, {});
}

std::filesystem::path recoveryPath(const std::filesystem::path &source, std::string_view recoveryId) {
    const auto name = source.filename().empty() ? "untitled.pmx" : source.filename().string();
    std::uint64_t hash = 1469598103934665603ULL;
    auto key = source.lexically_normal().string();
    key.append(recoveryId);
    for (const auto value : key) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ULL;
    }
    std::ostringstream suffix;
    suffix << std::hex << std::setw(16) << std::setfill('0') << hash;
    return recoveryDirectory() / (name + "-" + suffix.str() + ".recovery.pmx");
}

} // namespace pmxer
