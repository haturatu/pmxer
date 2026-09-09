#include "RecoveryController.hpp"

#include "../platform/AtomicFile.hpp"
#include "../platform/Log.hpp"
#include "../platform/Paths.hpp"

#include <mmd/pmx.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace pmxer {
namespace {

std::string escapeJson(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto character : value) {
        if (character == '\\' || character == '"')
            escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

} // namespace

RecoveryResult writeRecovery(DocumentSession &session) {
    if (!session.hasUnsavedWork())
        return {false, {}};
    if (session.hasPendingTransformEdit()) {
        log::warn("temporary Transform View edits are not included in recovery data; capture or discard them first");
        if (!session.modified)
            return {false, {}};
    }
    const auto path = session.path.empty() ? recoveryPath(session.path, session.recoveryId) : recoveryPath(session.path);
    const auto temporary = temporarySibling(path);
    try {
        std::filesystem::create_directories(path.parent_path());
        mmd::PmxSaveOptions options;
        options.mode = mmd::PmxSaveMode::preserve;
        options.indexWidths = mmd::PmxIndexWidthPolicy::preserveAndWiden;
        (void)mmd::pmx::save(temporary, session.document.model(), options);
        if (!atomicReplace(temporary, path)) {
            std::filesystem::remove(temporary);
            return {false, {}};
        }
        std::ofstream metadata(path.string() + ".json", std::ios::trunc);
        metadata << "{\n  \"source\": \"" << escapeJson(session.path.string()) << "\",\n  \"modified\": true\n}\n";
        if (!metadata)
            log::warn("回復メタデータの保存に失敗しました");
        if (session.recoveryFile && *session.recoveryFile != path)
            (void)discardRecoveryFile(*session.recoveryFile);
        session.recoveryFile = path;
        log::info("回復情報を保存しました");
        return {true, path};
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        log::warn("回復情報の保存に失敗しました");
        return {false, {}};
    }
}

std::optional<mmd::PmxModel> loadRecovery(const std::filesystem::path &source) {
    const auto path = recoveryPath(source);
    if (!std::filesystem::exists(path))
        return std::nullopt;
    try {
        return mmd::pmx::load(path);
    } catch (...) {
        return std::nullopt;
    }
}

bool discardRecovery(const std::filesystem::path &source) {
    std::error_code error;
    std::filesystem::remove(recoveryPath(source), error);
    std::filesystem::remove(recoveryPath(source).string() + ".json", error);
    return !error;
}

std::vector<RecoveryEntry> findUntitledRecoveries() {
    std::vector<RecoveryEntry> result;
    std::error_code error;
    if (!std::filesystem::is_directory(recoveryDirectory(), error))
        return result;
    for (const auto &entry : std::filesystem::directory_iterator(recoveryDirectory(), error)) {
        if (error || !entry.is_regular_file(error) || entry.path().extension() != ".pmx")
            continue;
        const auto metadataPath = entry.path().string() + ".json";
        std::ifstream metadata(metadataPath);
        const std::string contents((std::istreambuf_iterator<char>(metadata)), std::istreambuf_iterator<char>());
        if (contents.find("\"source\": \"\"") == std::string::npos)
            continue;
        try {
            result.push_back({entry.path(), mmd::pmx::load(entry.path())});
        } catch (...) {
            log::warn("回復情報の読み込みに失敗しました");
        }
    }
    return result;
}

bool discardRecoveryFile(const std::filesystem::path &path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + ".json", error);
    return !error;
}

} // namespace pmxer
