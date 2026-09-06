#include "AtomicFile.hpp"

#include <chrono>
#include <string>

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
    std::filesystem::remove(destination, error);
    if (error)
        return false;
    error.clear();
    std::filesystem::rename(temporary, destination, error);
#endif
    return !error;
}

} // namespace pmxer

