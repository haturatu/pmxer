#include "ResourceLocator.hpp"

#include <cstdlib>
#include <optional>
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

std::filesystem::path resolveFileCase(const std::filesystem::path &path) {
    if (path.empty() || isRegularFile(path))
        return path;
    const auto fold = [](const std::filesystem::path &name) {
        auto value = name.generic_u8string();
        for (auto &c : value)
            if (c >= u8'A' && c <= u8'Z')
                c = static_cast<char8_t>(c + (u8'a' - u8'A'));
        return value;
    };
    const auto normalized = path.lexically_normal();
    auto resolved = normalized.root_path();
    for (const auto &part : normalized.relative_path()) {
        const auto candidate = resolved / part;
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) {
            resolved = candidate;
            continue;
        }
        std::optional<std::filesystem::path> match;
        const auto name = fold(part);
        auto entries = std::filesystem::directory_iterator(
            resolved.empty() ? std::filesystem::path{"."} : resolved, error);
        const auto end = std::filesystem::directory_iterator{};
        for (; !error && entries != end; entries.increment(error)) {
            if (fold(entries->path().filename()) != name)
                continue;
            if (match)
                return path; // Never choose arbitrarily between two files.
            match = entries->path();
        }
        if (error || !match)
            return path;
        resolved = *match;
    }
    return isRegularFile(resolved) ? resolved : path;
}

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
                                    const std::filesystem::path &resourceDirectory,
                                    bool simplifiedChinese) {
    const std::vector<std::filesystem::path> candidates = {
        requested,
        environmentPath("PMXER_FONT"),
        simplifiedChinese ? resourceDirectory / "fonts" / "NotoSansCJKsc-Regular.otf" : std::filesystem::path{},
        simplifiedChinese ? resourceDirectory / "fonts" / "NotoSansSC-Regular.ttf" : std::filesystem::path{},
        simplifiedChinese ? std::filesystem::path{"/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf"} : std::filesystem::path{},
        simplifiedChinese ? std::filesystem::path{"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"} : std::filesystem::path{},
        simplifiedChinese ? std::filesystem::path{"C:/Windows/Fonts/msyh.ttc"} : std::filesystem::path{},
        simplifiedChinese ? std::filesystem::path{"/System/Library/Fonts/PingFang.ttc"} : std::filesystem::path{},
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
        resourceDirectory / "fonts" / "NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:/Windows/Fonts/msgothic.ttc",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
    };
    for (const auto &candidate : candidates)
        if (isRegularFile(candidate))
            return candidate;
    return {};
}

} // namespace pmxer
