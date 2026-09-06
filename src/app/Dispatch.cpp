#include "Dispatch.hpp"

#include "../commands/CommandOutput.hpp"
#include "../platform/Log.hpp"

#if PMXER_HAS_GUI
#include "../ui/MainWindow.hpp"
#endif

#include <cstdio>
#include <exception>

namespace pmxer {
namespace {

struct CommandVisitor {
    int operator()(const EditCommand &command) const {
#if PMXER_HAS_GUI
        return runApplication(command);
#else
        (void)command;
        log::error("this build of pmxer has no GUI support");
        return 3;
#endif
    }

    int operator()(const InfoCommand &command) const {
        return runInfoCommand(command);
    }

    int operator()(const ValidateCommand &command) const {
        return runValidateCommand(command);
    }

    int operator()(const DiffCommand &command) const {
        return runDiffCommand(command);
    }

    int operator()(const NormalizeCommand &command) const {
        return runNormalizeCommand(command);
    }

    int operator()(const HelpCommand &command) const {
        const auto text = usage(command.command);
        std::fputs(text.c_str(), stdout);
        return 0;
    }
};

} // namespace

int dispatch(const Invocation &invocation) {
    try {
        return std::visit(CommandVisitor{}, invocation.command);
    } catch (const std::exception &error) {
        log::error(error.what());
        return 3;
    }
}

} // namespace pmxer
