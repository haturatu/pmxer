#include "CommandLine.hpp"
#include "Dispatch.hpp"

#include <cstdio>
#include <nlohmann/json.hpp>
namespace {
void printJson(const nlohmann::json &value) {
    std::puts(value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
}
} // namespace
int main(int argc, char **argv) {
    const bool json = pmxer::requestsJsonOutput(argc, argv);
    const auto parsed = pmxer::parseInvocation(argc, argv);
    if (!parsed.error.empty()) {
        if (json) {
            const auto error = nlohmann::json{
                {"error", {{"code", "invalid_arguments"}, {"message", parsed.error}}}};
            printJson(error);
            std::fprintf(stderr, "ERROR %s\n", parsed.error.c_str());
        } else
            std::fprintf(stderr, "ERROR %s\n%s", parsed.error.c_str(), pmxer::usage().c_str());
        return 2;
    }
    if (!parsed.invocation.has_value()) {
        if (json)
            std::puts(nlohmann::json{{"error",
                                      {{"code", "invalid_arguments"},
                                       {"message", "No command invocation was produced"}}}}
                          .dump()
                          .c_str());
        else
            std::fputs(pmxer::usage().c_str(), stderr);
        return 2;
    }
    if (parsed.invocation->global.version) {
        if (json)
            printJson(nlohmann::json{{"version", pmxer::applicationVersion()}});
        else
            std::printf("%s\n", pmxer::applicationVersion().c_str());
        return 0;
    }
    if (json)
        if (const auto *help = std::get_if<pmxer::HelpCommand>(&parsed.invocation->command)) {
            printJson(nlohmann::json{{"help", pmxer::usage(help->command)}});
            return 0;
        }
    return pmxer::dispatch(*parsed.invocation);
}
