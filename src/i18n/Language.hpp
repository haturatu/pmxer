#pragma once

#include <span>
#include <string_view>
#include <optional>

namespace pmxer::i18n {

enum class Locale { japanese, english, chineseSimplified, russian };

struct Message {
    std::string_view source;
    std::string_view japanese;
    std::string_view english;
    std::string_view chineseSimplified;
    std::string_view russian;
};

// Accept BCP 47 tags and POSIX locale names (for example, ja-JP or ru_RU.UTF-8).
[[nodiscard]] std::optional<Locale> parseLocale(std::string_view name);
[[nodiscard]] std::string_view localeName(Locale locale) noexcept;
[[nodiscard]] Locale resolveLocale(std::string_view requested,
                                  std::span<const std::string_view> preferred = {});
// Select once at startup, before constructing editor sessions or UI controls.
void setLocale(Locale locale) noexcept;
[[nodiscard]] Locale locale() noexcept;
[[nodiscard]] std::span<const Message> messages() noexcept;
[[nodiscard]] std::string_view translation(const Message &message, Locale locale) noexcept;
// Unknown strings are returned unchanged. Source strings must outlive their use.
[[nodiscard]] const char *tr(std::string_view source);
// The ### suffix gives windows a language-independent ImGui ID and layout key.
[[nodiscard]] const char *windowTitle(std::string_view source);

} // namespace pmxer::i18n
