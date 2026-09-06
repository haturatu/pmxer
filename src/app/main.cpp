#include "CommandLine.hpp"
#include "Dispatch.hpp"

#include <cstdio>
int main(int argc, char **argv) {
    const auto parsed = pmxer::parseInvocation(argc, argv);
    if (!parsed.error.empty()) {
        std::fprintf(stderr, "ERROR %s\n%s", parsed.error.c_str(), pmxer::usage().c_str());
        return 2;
    }
    if (!parsed.invocation.has_value()) {
        std::fputs(pmxer::usage().c_str(), stderr);
        return 2;
    }
    if (parsed.invocation->global.version) {
        std::printf("%s\n", pmxer::applicationVersion().c_str());
        return 0;
    }
    return pmxer::dispatch(*parsed.invocation);
}
