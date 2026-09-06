#include "CommandLine.hpp"
#include "../ui/MainWindow.hpp"

#include <cstdio>
#include <filesystem>

int main(int argc, char **argv) {
    const auto parsed = pmxer::parseStartupArguments(argc, argv);
    if (!parsed.error.empty()) {
        std::fprintf(stderr, "ERROR %s\n%s", parsed.error.c_str(), pmxer::startupUsage().c_str());
        return 1;
    }
    if (parsed.options.help) {
        std::fputs(pmxer::startupUsage().c_str(), stdout);
        return 0;
    }
    if (parsed.options.version) {
        std::printf("%s\n", pmxer::applicationVersion().c_str());
        return 0;
    }
    return pmxer::runApplication(parsed.options);
}
