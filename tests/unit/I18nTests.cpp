#include "i18n/Language.hpp"
#include "editor/DocumentSession.hpp"
#include "editor/EditorDiagnostics.hpp"
#include "editor/SaveController.hpp"
#include "editor/tools/WeightTool.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<std::string_view> placeholders(std::string_view text) {
    std::vector<std::string_view> result;
    for (std::size_t start = 0; (start = text.find('%', start)) != text.npos; ++start) {
        const auto end = text.find_first_of("diuoxXfFeEgGaAcspn%", start + 1);
        require(end != text.npos, "incomplete format specifier");
        result.push_back(text.substr(start, end - start + 1));
        start = end;
    }
    return result;
}

std::string_view suffix(std::string_view label) {
    const auto start = label.find("##");
    return start == label.npos ? std::string_view{} : label.substr(start);
}

void setEnvironment(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
} // namespace

int main() {
    try {
        using namespace pmxer::i18n;
        require(parseLocale("ja-JP") == Locale::japanese, "Japanese tag");
        require(parseLocale("en_US.UTF-8") == Locale::english, "English POSIX name");
        require(parseLocale("ZH-hans-CN") == Locale::chineseSimplified, "Chinese script tag");
        require(parseLocale("ru_RU.UTF-8") == Locale::russian, "Russian POSIX name");
        require(!parseLocale("zh-Hant-TW"), "Traditional Chinese must not be misidentified");
        require(!parseLocale("fr") && !parseLocale(""), "unsupported languages");
        require(resolveLocale("unknown") == Locale::english, "unsupported locale fallback");
        for (const auto *name : {"PMXER_LANG", "LC_ALL", "LC_MESSAGES", "LANG"})
            setEnvironment(name, "");
        const std::array preferred{std::string_view{"de-DE"}, std::string_view{"ru-RU"}};
        require(resolveLocale("auto", preferred) == Locale::russian, "native preferred locales");
        require(resolveLocale("auto") == Locale::english, "no locale fallback");
        setEnvironment("LANG", "ja_JP.UTF-8");
        require(resolveLocale("auto") == Locale::japanese, "LANG detection");
        setEnvironment("LC_MESSAGES", "ru_RU.UTF-8");
        require(resolveLocale("auto") == Locale::russian, "LC_MESSAGES priority");
        setEnvironment("LC_ALL", "zh_CN.UTF-8");
        require(resolveLocale("auto") == Locale::chineseSimplified, "LC_ALL priority");
        setEnvironment("PMXER_LANG", "en");
        require(resolveLocale("auto") == Locale::english, "application override priority");
        require(resolveLocale("ja") == Locale::japanese, "explicit language overrides environment");
        setEnvironment("PMXER_LANG", "unsupported");
        require(resolveLocale("auto") == Locale::english, "unsupported environment fallback");

        constexpr std::array locales{Locale::japanese, Locale::english,
                                    Locale::chineseSimplified, Locale::russian};
        std::set<std::string_view> keys;
        for (const auto &message : messages()) {
            require(keys.insert(message.source).second, "duplicate catalog key");
            for (const auto language : locales) {
                const auto value = translation(message, language);
                require(!value.empty(), "missing translation");
                require(placeholders(message.source) == placeholders(value), "format specifiers differ");
                require(suffix(message.source) == suffix(value), "ImGui suffix differs");
                setLocale(language);
                require(tr(message.source) == value, "catalog lookup failed");
            }
        }
        require(messages().size() > 700, "incomplete catalog");
        for (const auto language : locales) {
            setLocale(language);
            require(std::string_view{tr("保存")} != "", "save translation");
            const std::string title{windowTitle("ビューポート")};
            require(title.ends_with("###pmxer.window.ビューポート"), "window ID must be stable");
            require(title.starts_with(tr("ビューポート")), "window title must be localized");
            require(std::string_view{tr("User model name / Пользователь / 用户")} ==
                    "User model name / Пользователь / 用户", "unknown strings must be unchanged");

            mmd::PmxModel model;
            model.metadata.version = 2.1F;
            model.metadata.modelName = "ユーザーモデル / 用户 / Модель";
            for (const auto *name : {"左腕", "右腕"}) {
                mmd::PmxBone bone;
                bone.name = name;
                model.bones.push_back(bone);
            }
            model.vertices.resize(2);
            for (auto &vertex : model.vertices) {
                vertex.bones[0] = 0;
                vertex.weights[0] = 1.0F;
            }
            pmxer::DocumentSession session{model};
            require(pmxer::mirrorWeights(session, {session.document.vertexHandle(0)},
                                         {session.document.vertexHandle(1)}), "mirror weights failed");
            require(session.document.model().vertices[1].bones[0] == 1,
                    "Japanese bone-name matching must be independent of UI language");
            require(session.document.model().metadata.modelName == model.metadata.modelName &&
                    session.document.model().bones[0].name == "左腕" &&
                    session.document.model().bones[1].name == "右腕", "model data was translated");
            require(pmxer::saveDocument(session).message == tr("保存先が指定されていません"),
                    "save error must be localized");
            mmd::PmxModel unnamed;
            unnamed.metadata.version = 2.1F;
            unnamed.bones.emplace_back();
            const auto diagnostics = pmxer::validateForEditing(mmd::PmxDocument{unnamed});
            bool localizedNameWarning = false;
            for (const auto &issue : diagnostics.issues)
                localizedNameWarning |= issue.message == tr("名前が空です");
            require(localizedNameWarning, "editor diagnostics must be localized");
        }
        setLocale(Locale::russian);
        char buffer[128]{};
        std::snprintf(buffer, sizeof(buffer), tr("頂点 %zu / 面 %zu"), std::size_t{21}, std::size_t{5});
        require(std::string_view{buffer} == "Вершины: 21 / Грани: 5", "Russian counts must be grammatical for any number");
        setLocale(Locale::english);
        require(std::string_view{tr("保存")} == "Save", "English save label");
        setLocale(Locale::chineseSimplified);
        require(std::string_view{tr("保存")} == "保存", "Chinese save label");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
