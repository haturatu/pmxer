#include "SaveController.hpp"

#include "../platform/AtomicFile.hpp"
#include "../platform/Log.hpp"
#include "../platform/Paths.hpp"
#include "RecoveryController.hpp"

#include <filesystem>
#include <limits>
#include <utility>

namespace pmxer {

SaveResult saveDocument(DocumentSession &session, const std::filesystem::path &destinationArgument) {
    const auto destination = destinationArgument.empty() ? session.path : destinationArgument;
    if (destination.empty())
        return {false, "保存先が指定されていません", {}, {}};

    session.validation = session.document.validate();
    if (!session.validation.valid())
        return {false, "検証エラーがあるため保存できません", {}, {}};

    const auto temporary = temporarySibling(destination);
    const auto oldRecovery = session.recoveryFile.value_or(
        session.path.empty() ? recoveryPath({}, session.recoveryId) : recoveryPath(session.path));
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
        const auto sourceChanged = session.document.model().sourcePath != destination;
        session.path = destination;
        session.document.setSourcePath(destination);
        written.sourcePath = destination;
        if (sourceChanged) {
            ++session.resourceRevision;
            session.derived.diagnosticsRevision = std::numeric_limits<std::uint64_t>::max();
        }
        session.derived.diffRevision = std::numeric_limits<std::uint64_t>::max();
        session.commands.markClean();
        session.modified = session.commands.isModified();
        session.baseline = std::move(written);
        (void)discardRecoveryFile(oldRecovery);
        session.recoveryFile.reset();
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
