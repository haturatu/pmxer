#include "../../src/app/CommandLine.hpp"

#include <cassert>
#include <variant>

int main() {
    char name[] = "pmxer";
    char firstModel[] = "first.pmx";
    char secondModel[] = "second.pmx";
    char *editArguments[] = {name, firstModel, secondModel};
    const auto edit = pmxer::parseInvocation(3, editArguments);
    assert(edit.error.empty());
    assert(edit.invocation.has_value());
    assert(std::holds_alternative<pmxer::EditCommand>(edit.invocation->command));
    assert(std::get<pmxer::EditCommand>(edit.invocation->command).documents.size() == 2);

    char infoCommand[] = "info";
    char jsonOption[] = "--json";
    char model[] = "model.pmx";
    char *infoArguments[] = {name, infoCommand, jsonOption, model};
    const auto info = pmxer::parseInvocation(4, infoArguments);
    assert(info.error.empty());
    assert(std::holds_alternative<pmxer::InfoCommand>(info.invocation->command));
    assert(std::get<pmxer::InfoCommand>(info.invocation->command).format == pmxer::OutputFormat::json);

    char validateCommand[] = "validate";
    char *validateArguments[] = {name, validateCommand, firstModel, secondModel};
    const auto validate = pmxer::parseInvocation(4, validateArguments);
    assert(validate.error.empty());
    assert(std::holds_alternative<pmxer::ValidateCommand>(validate.invocation->command));
    assert(std::get<pmxer::ValidateCommand>(validate.invocation->command).models.size() == 2);

    char diffCommand[] = "diff";
    char profileOption[] = "--profile";
    char preservation[] = "preservation";
    char *diffArguments[] = {name, diffCommand, profileOption, preservation, firstModel, secondModel};
    const auto diff = pmxer::parseInvocation(6, diffArguments);
    assert(diff.error.empty());
    assert(std::holds_alternative<pmxer::DiffCommand>(diff.invocation->command));
    assert(std::get<pmxer::DiffCommand>(diff.invocation->command).profile == mmd::PmxComparisonProfile::preservation);

    char normalizeCommand[] = "normalize";
    char outputOption[] = "-o";
    char output[] = "normalized.pmx";
    char *normalizeArguments[] = {name, normalizeCommand, outputOption, output, model};
    const auto normalize = pmxer::parseInvocation(5, normalizeArguments);
    assert(normalize.error.empty());
    assert(std::holds_alternative<pmxer::NormalizeCommand>(normalize.invocation->command));
    assert(std::get<pmxer::NormalizeCommand>(normalize.invocation->command).output == output);

    char helpCommand[] = "help";
    char helpTarget[] = "validate";
    char *helpArguments[] = {name, helpCommand, helpTarget};
    const auto help = pmxer::parseInvocation(3, helpArguments);
    assert(help.error.empty());
    assert(std::holds_alternative<pmxer::HelpCommand>(help.invocation->command));
    assert(std::get<pmxer::HelpCommand>(help.invocation->command).command == "validate");

    char separator[] = "--";
    char infoFile[] = "info";
    char *fileArguments[] = {name, separator, infoFile};
    const auto file = pmxer::parseInvocation(3, fileArguments);
    assert(file.error.empty());
    assert(std::holds_alternative<pmxer::EditCommand>(file.invocation->command));
    assert(std::get<pmxer::EditCommand>(file.invocation->command).documents.front() == "info");

    char optionLikeFile[] = "--foo.pmx";
    char *optionLikeArguments[] = {name, separator, optionLikeFile};
    const auto optionLike = pmxer::parseInvocation(3, optionLikeArguments);
    assert(optionLike.error.empty());
    assert(std::holds_alternative<pmxer::EditCommand>(optionLike.invocation->command));
    assert(std::get<pmxer::EditCommand>(optionLike.invocation->command).documents.front() == "--foo.pmx");

    char infoHelp[] = "--help";
    char *infoHelpArguments[] = {name, infoCommand, infoHelp};
    const auto infoHelpResult = pmxer::parseInvocation(3, infoHelpArguments);
    assert(infoHelpResult.error.empty());
    assert(std::holds_alternative<pmxer::HelpCommand>(infoHelpResult.invocation->command));
    assert(std::get<pmxer::HelpCommand>(infoHelpResult.invocation->command).command == "info");

    char rendererOption[] = "--renderer=vulkan";
    char fontSizeOption[] = "--font-size=24";
    char *configuredEditArguments[] = {name, rendererOption, fontSizeOption, firstModel};
    const auto configuredEdit = pmxer::parseInvocation(4, configuredEditArguments);
    assert(configuredEdit.error.empty());
    const auto &configured = std::get<pmxer::EditCommand>(configuredEdit.invocation->command);
    assert(configured.renderer == "vulkan");
    assert(configured.fontSize == 24.0F);

    char emptyFontSize[] = "--font-size=";
    char *emptyFontSizeArguments[] = {name, emptyFontSize};
    const auto emptyFontSizeResult = pmxer::parseInvocation(2, emptyFontSizeArguments);
    assert(!emptyFontSizeResult.error.empty());

    char invalidRenderer[] = "--renderer=opengl";
    char *invalidRendererArguments[] = {name, invalidRenderer};
    const auto invalidRendererResult = pmxer::parseInvocation(2, invalidRendererArguments);
    assert(!invalidRendererResult.error.empty());

    char uiCommand[] = "ui";
    char treeOperation[] = "tree";
    char *uiArguments[] = {name, uiCommand, treeOperation};
    const auto ui = pmxer::parseInvocation(3, uiArguments);
    assert(ui.error.empty());
    assert(ui.invocation.has_value());
    assert(std::holds_alternative<pmxer::UiCommand>(ui.invocation->command));
    assert(std::get<pmxer::UiCommand>(ui.invocation->command).operation == "tree");

    char setOperation[] = "set";
    char target[] = "inspector/material.name";
    char value[] = "制服";
    char *uiSetArguments[] = {name, uiCommand, setOperation, target, value};
    const auto uiSet = pmxer::parseInvocation(5, uiSetArguments);
    assert(uiSet.error.empty());
    const auto &setCommand = std::get<pmxer::UiCommand>(uiSet.invocation->command);
    assert(setCommand.target == target);
    assert(setCommand.value == value);

    char automationOption[] = "--automation=/tmp/pmxer-test.sock";
    char *automationArguments[] = {name, automationOption, firstModel};
    const auto automation = pmxer::parseInvocation(3, automationArguments);
    assert(automation.error.empty());
    const auto &editWithAutomation =
        std::get<pmxer::EditCommand>(automation.invocation->command);
    assert(editWithAutomation.automation);
    assert(editWithAutomation.automationSocket == "/tmp/pmxer-test.sock");
    return 0;
}
