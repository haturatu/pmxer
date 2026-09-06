#include "../../src/app/CommandLine.hpp"

#include <cassert>

int main() {
    char startupName[] = "pmxer";
    char helpOption[] = "--help";
    char firstModel[] = "first.pmx";
    char secondModel[] = "second.pmx";
    char *startupArguments[] = {startupName, helpOption, firstModel, secondModel};
    const auto startup = pmxer::parseStartupArguments(4, startupArguments);
    assert(startup.error.empty());
    assert(startup.options.help);
    assert(startup.options.documents.size() == 2);

    char cliName[] = "pmxer-cli";
    char diffCommand[] = "diff";
    char jsonOption[] = "--json";
    char profileOption[] = "--profile";
    char preservation[] = "preservation";
    char leftModel[] = "left.pmx";
    char rightModel[] = "right.pmx";
    char *cliArguments[] = {cliName, diffCommand, jsonOption, profileOption, preservation, leftModel, rightModel};
    const auto cli = pmxer::parseCliArguments(7, cliArguments);
    assert(cli.error.empty());
    assert(cli.options.command == "diff");
    assert(cli.options.json);
    assert(cli.options.profile == "preservation");
    assert(cli.options.operands.size() == 2);

    char cliHelp[] = "--help";
    char *helpArguments[] = {cliName, cliHelp};
    const auto cliHelpResult = pmxer::parseCliArguments(2, helpArguments);
    assert(cliHelpResult.error.empty());
    assert(cliHelpResult.options.help);
    return 0;
}
