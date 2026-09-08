#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace pmxer::automation {

[[nodiscard]] inline std::string escapeJson(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2U);
    result.push_back('"');
    for (const auto character : value) {
        switch (character) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                result += "\\u00";
                constexpr char digits[] = "0123456789abcdef";
                result.push_back(digits[(static_cast<unsigned char>(character) >> 4U) & 0x0FU]);
                result.push_back(digits[static_cast<unsigned char>(character) & 0x0FU]);
            } else {
                result.push_back(character);
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] inline std::filesystem::path defaultSocketPath() {
#if defined(_WIN32)
    return std::filesystem::temp_directory_path() / "pmxer-automation.sock";
#else
    const auto user = static_cast<unsigned long long>(::getuid());
    return std::filesystem::path{"/tmp"} /
           ("pmxer-" + std::to_string(user) + ".sock");
#endif
}

} // namespace pmxer::automation
