#pragma once

#include <cstdio>

namespace pmxer::log {

inline void debug(const char *message) noexcept {
    std::fprintf(stdout, "DEBUG %s\n", message);
}
inline void info(const char *message) noexcept {
    std::fprintf(stdout, "INFO %s\n", message);
}
inline void warn(const char *message) noexcept {
    std::fprintf(stderr, "WARN %s\n", message);
}
inline void error(const char *message) noexcept {
    std::fprintf(stderr, "ERROR %s\n", message);
}

} // namespace pmxer::log

