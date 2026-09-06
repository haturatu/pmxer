#include "SaveController.hpp"

#include "../platform/AtomicFile.hpp"
#include "../platform/Log.hpp"
#include "RecoveryController.hpp"

#include <filesystem>

namespace pmxer {

SaveResult saveDocument(DocumentSession &session, const std::filesystem::path &destinationArgument) {
    const auto destination = destinationArgument.empty() ? session.path : destinationArgument;
    if (destination.empty())
        return {false, "保存先が指定されていません", {}, {}};

    session.validation = session.document.validate();
    if (!session.validation.valid())
        return {false, "検証エラーがあるため保存できません", {}, {}};

    const auto temporary = temporarySibling(destination);
    SaveResult result;
    try {
        mmd::PmxSaveOptions options;
        options.mode = mmd::PmxSaveMode::preserve;
        options.indexWidths = mmd::PmxIndexWidthPolicy::preserveAndWiden;
        result.report = mmd::pmx::save(temporary, session.document.model(), options);
        const auto written = mmd::pmx::load(temporary);
        result.verification = mmd::pmx::semanticCompare(
            session.document.model(), written, mmd::PmxComparisonProfile::preservation);
        if (!result.verification.equal()) {
            std::filesystem::remove(temporary);
            result.message = "保存後の意味比較に失敗しました";
            log::error(result.message.c_str());
            return result;
        }
        if (!atomicReplace(temporary, destination)) {
            std::filesystem::remove(temporary);
            result.message = "保存先を原子置換できませんでした";
            log::error(result.message.c_str());
            return result;
        }
        session.path = destination;
        session.commands.markClean();
        session.modified = session.commands.isModified();
        session.baseline = written;
        (void)discardRecovery(destination);
        result.success = true;
        result.message = "保存しました";
        log::info(result.message.c_str());
        return result;
    } catch (const std::exception &error) {
        std::filesystem::remove(temporary);
        result.message = error.what();
        log::error(result.message.c_str());
        return result;
    }
}

} // namespace pmxer
