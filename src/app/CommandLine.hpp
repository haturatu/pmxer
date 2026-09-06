#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pmxer {

struct StartupOptions {
    bool help{};
    bool version{};
    bool gpuDebug{};
    bool physics{true};
    bool safeMode{};
    std::string renderer{"auto"};
    std::string locale{"auto"};
    std::filesystem::path font;
    std::filesystem::path resourceDirectory;
    float fontSize{18.0F};
    std::vector<std::filesystem::path> documents;
};

struct StartupParseResult {
    StartupOptions options;
    std::string error;
};

[[nodiscard]] StartupParseResult parseStartupArguments(int argc, char *const argv[]);
[[nodiscard]] std::string startupUsage();
[[nodiscard]] std::string applicationVersion();

struct CliOptions {
    std::string command;
    std::string helpCommand;
    std::string profile{"logical"};
    std::filesystem::path output;
    std::vector<std::filesystem::path> operands;
    bool help{};
    bool json{};
};

struct CliParseResult {
    CliOptions options;
    std::string error;
};

[[nodiscard]] CliParseResult parseCliArguments(int argc, char *const argv[]);
[[nodiscard]] std::string cliUsage(std::string_view command = {});

} // namespace pmxer
