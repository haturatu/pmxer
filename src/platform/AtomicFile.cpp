#include "AtomicFile.hpp"

#include <chrono>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pmxer {

std::filesystem::path temporarySibling(const std::filesystem::path &destination) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.string() + ".pmxer-tmp-" + std::to_string(stamp);
}

bool atomicReplace(const std::filesystem::path &temporary, const std::filesystem::path &destination) {
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error)
        return true;
#ifdef _WIN32
    if (ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
        return true;

    const auto replaceError = GetLastError();
    if (replaceError == ERROR_FILE_NOT_FOUND &&
        MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING))
        return true;

    return false;
#else
    return false;
#endif
}

} // namespace pmxer
