#include "ResourceLocator.hpp"

#include <cstdlib>
#include <system_error>
#include <vector>

namespace pmxer {
namespace {

std::filesystem::path environmentPath(const char *name) {
    const auto *value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::filesystem::path{} : std::filesystem::path(value);
}

bool isDirectory(const std::filesystem::path &path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path, error);
}

bool isRegularFile(const std::filesystem::path &path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error);
}

} // namespace

std::filesystem::path resolveResourceDirectory(const std::filesystem::path &requested,
                                                const std::filesystem::path &executableDirectory) {
    const std::vector<std::filesystem::path> candidates = {
        requested,
        environmentPath("PMXER_RESOURCE_DIR"),
        executableDirectory / "assets",
        executableDirectory / ".." / "share" / "pmxer" / "assets",
        executableDirectory / ".." / "share" / "pmxer",
    };
    for (const auto &candidate : candidates)
        if (isDirectory(candidate))
            return std::filesystem::weakly_canonical(candidate);
    return requested.empty() ? executableDirectory / "assets" : requested;
}

std::filesystem::path resolveUiFont(const std::filesystem::path &requested,
                                    const std::filesystem::path &resourceDirectory) {
    const std::vector<std::filesystem::path> candidates = {
        requested,
        environmentPath("PMXER_FONT"),
        resourceDirectory / "fonts" / "NotoSansCJKjp-Regular.otf",
        resourceDirectory / "fonts" / "NotoSansJP-Regular.otf",
        resourceDirectory / "fonts" / "NotoSansCJK-Regular.ttc",
        resourceDirectory / "fonts" / "NotoSansCJK-Medium.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansJP-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Medium.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansJP-Regular.ttf",
        "/usr/share/fonts/truetype/droid/DroidSansJapanese.ttf",
        "/usr/share/fonts/droid/DroidSansJapanese.ttf",
        "C:/Windows/Fonts/msgothic.ttc",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
    };
    for (const auto &candidate : candidates)
        if (isRegularFile(candidate))
            return candidate;
    return {};
}

} // namespace pmxer
