#include "Language.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace pmxer::i18n {
namespace {
using namespace std::literals;
#define PMXER_MESSAGE(source, ja, en, zh, ru) Message{source##sv, ja##sv, en##sv, zh##sv, ru##sv},
constexpr Message catalog[]{
#include "Catalog.inc"
};
#undef PMXER_MESSAGE

// Keep library callers deterministic; the application explicitly resolves auto.
Locale activeLocale = Locale::japanese;

std::string_view environment(const char *name) {
    const auto *value = std::getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}
} // namespace

std::optional<Locale> parseLocale(std::string_view name) {
    std::string normalized{name};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    std::ranges::replace(normalized, '_', '-');
    if (const auto encoding = normalized.find_first_of(".@"); encoding != std::string::npos)
        normalized.resize(encoding);
    const auto end = normalized.find('-');
    const auto language = normalized.substr(0, end);
    if (language == "ja")
        return Locale::japanese;
    if (language == "en")
        return Locale::english;
    if (language == "zh") {
        // Traditional Chinese is not a Simplified Chinese locale.
        if (normalized.find("-hant") != std::string::npos ||
            normalized.find("-tw") != std::string::npos ||
            normalized.find("-hk") != std::string::npos ||
            normalized.find("-mo") != std::string::npos)
            return std::nullopt;
        return Locale::chineseSimplified;
    }
    if (language == "ru")
        return Locale::russian;
    return std::nullopt;
}

std::string_view localeName(Locale value) noexcept {
    switch (value) {
    case Locale::japanese: return "ja";
    case Locale::english: return "en";
    case Locale::chineseSimplified: return "zh-CN";
    case Locale::russian: return "ru";
    }
    return "en";
}

Locale resolveLocale(std::string_view requested,
                     std::span<const std::string_view> preferred) {
    if (requested != "auto")
        return parseLocale(requested).value_or(Locale::english);
    for (const auto *name : {"PMXER_LANG", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        const auto value = environment(name);
        if (!value.empty() && value != "auto")
            return parseLocale(value).value_or(Locale::english);
    }
    for (const auto value : preferred)
        if (const auto supported = parseLocale(value))
            return *supported;
    return Locale::english;
}

void setLocale(Locale value) noexcept { activeLocale = value; }
Locale locale() noexcept { return activeLocale; }
std::span<const Message> messages() noexcept { return catalog; }

std::string_view translation(const Message &message, Locale value) noexcept {
    switch (value) {
    case Locale::japanese: return message.japanese;
    case Locale::english: return message.english;
    case Locale::chineseSimplified: return message.chineseSimplified;
    case Locale::russian: return message.russian;
    }
    return message.english;
}

const char *tr(std::string_view source) {
    if (source.empty())
        return "";
    static const auto index = [] {
        std::unordered_map<std::string_view, const Message *> result;
        for (const auto &message : catalog)
            result.emplace(message.source, &message);
        return result;
    }();
    if (const auto found = index.find(source); found != index.end()) {
        const auto value = translation(*found->second, activeLocale);
        return (value.empty() ? found->second->english : value).data();
    }
    return source.data();
}

const char *windowTitle(std::string_view source) {
    static std::unordered_map<std::string, std::string> titles;
    const auto key = std::string{localeName(activeLocale)} + ':' + std::string{source};
    auto [entry, inserted] = titles.try_emplace(key);
    if (inserted)
        entry->second = std::string{tr(source)} + "###pmxer.window." + std::string{source};
    return entry->second.c_str();
}

} // namespace pmxer::i18n
