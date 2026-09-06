#include "CommandLine.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace pmxer {
namespace {

bool splitOption(const std::string &argument, const char *name, std::string &value) {
    const std::string prefix = std::string(name) + "=";
    if (argument.rfind(prefix, 0) != 0)
        return false;
    value = argument.substr(prefix.size());
    return true;
}

bool isRenderer(const std::string &value) {
    return value == "auto" || value == "vulkan" || value == "direct3d12" || value == "metal";
}

bool parseFontSize(const std::string &value, float &result) {
    char *end = nullptr;
    const auto parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed < 6.0F || parsed > 256.0F)
        return false;
    result = parsed;
    return true;
}

bool takeValue(int &index, int argc, char *const argv[], const std::string &argument, const char *name,
               std::string &value, std::string &error) {
    if (splitOption(argument, name, value)) {
        if (!value.empty())
            return true;
        error = std::string(name) + " requires a value";
        return false;
    }
    if (argument != name)
        return false;
    if (index + 1 >= argc) {
        error = std::string(name) + " requires a value";
        return false;
    }
    value = argv[++index];
    if (value.empty()) {
        error = std::string(name) + " requires a value";
        return false;
    }
    return true;
}

} // namespace

StartupParseResult parseStartupArguments(int argc, char *const argv[]) {
    StartupParseResult result;
    bool positionalOnly = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? std::string{} : argv[index];
        if (positionalOnly || argument.empty() || argument[0] != '-') {
            result.options.documents.emplace_back(argument);
            continue;
        }
        if (argument == "--") {
            positionalOnly = true;
            continue;
        }
        if (argument == "-h" || argument == "--help") {
            result.options.help = true;
            continue;
        }
        if (argument == "-v" || argument == "--version") {
            result.options.version = true;
            continue;
        }
        if (argument == "--gpu-debug") {
            result.options.gpuDebug = true;
            continue;
        }
        if (argument == "--no-physics") {
            result.options.physics = false;
            continue;
        }
        if (argument == "--safe-mode") {
            result.options.safeMode = true;
            result.options.physics = false;
            continue;
        }

        std::string value;
        if (takeValue(index, argc, argv, argument, "--renderer", value, result.error)) {
            if (!isRenderer(value))
                result.error = "invalid renderer: " + value;
            else
                result.options.renderer = value;
            continue;
        }
        if (!result.error.empty())
            return result;
        if (takeValue(index, argc, argv, argument, "--font", value, result.error)) {
            result.options.font = value;
            continue;
        }
        if (!result.error.empty())
            return result;
        if (takeValue(index, argc, argv, argument, "--font-size", value, result.error)) {
            if (!parseFontSize(value, result.options.fontSize))
                result.error = "invalid font size: " + value;
            continue;
        }
        if (!result.error.empty())
            return result;
        if (takeValue(index, argc, argv, argument, "--lang", value, result.error)) {
            result.options.locale = value;
            continue;
        }
        if (!result.error.empty())
            return result;
        if (takeValue(index, argc, argv, argument, "--resource-dir", value, result.error)) {
            result.options.resourceDirectory = value;
            continue;
        }
        if (!result.error.empty())
            return result;
        result.error = "unknown option: " + argument;
        return result;
    }
    return result;
}

CliParseResult parseCliArguments(int argc, char *const argv[]) {
    CliParseResult result;
    if (argc < 2) {
        result.options.help = true;
        return result;
    }
    result.options.command = argv[1] == nullptr ? std::string{} : argv[1];
    if (result.options.command == "-h" || result.options.command == "--help") {
        result.options.command = "help";
        result.options.help = true;
        return result;
    }
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? std::string{} : argv[index];
        if (argument == "-h" || argument == "--help") {
            result.options.help = true;
            continue;
        }
        if (argument == "--json") {
            result.options.json = true;
            continue;
        }
        if (argument == "--profile") {
            if (index + 1 >= argc) {
                result.error = "--profile requires a value";
                return result;
            }
            result.options.profile = argv[++index];
        } else if (argument.rfind("--profile=", 0) == 0) {
            result.options.profile = argument.substr(std::string("--profile=").size());
        } else if (argument == "-o" || argument == "--output") {
            if (index + 1 >= argc) {
                result.error = "--output requires a value";
                return result;
            }
            result.options.output = argv[++index];
        } else if (argument.rfind("--output=", 0) == 0) {
            result.options.output = argument.substr(std::string("--output=").size());
        } else if (!argument.empty() && argument[0] == '-') {
            result.error = "unknown option: " + argument;
            return result;
        } else if (result.options.command == "help" && result.options.helpCommand.empty()) {
            result.options.helpCommand = argument;
        } else {
            result.options.operands.emplace_back(argument);
        }
    }
    if (result.options.command == "help")
        result.options.help = true;
    else if (result.options.help)
        result.options.helpCommand = result.options.command;
    if (result.options.profile != "logical" && result.options.profile != "preservation")
        result.error = "invalid comparison profile: " + result.options.profile;
    return result;
}

std::string startupUsage() {
    return "Usage: pmxer [options] [file.pmx ...]\n"
           "\n"
           "Options:\n"
           "  -h, --help                 Show this help\n"
           "  -v, --version              Show the application version\n"
           "      --renderer <name>      auto, vulkan, direct3d12, or metal\n"
           "      --gpu-debug             Enable GPU validation\n"
           "      --font <path>            Use a UI font\n"
           "      --font-size <size>      Set the UI font size\n"
           "      --lang <locale>         Set the UI locale\n"
           "      --no-physics             Disable preview physics\n"
           "      --safe-mode              Disable optional preview features\n"
           "      --resource-dir <path>  Set the resource directory\n";
}

std::string applicationVersion() {
    return "0.1.0";
}

std::string cliUsage(std::string_view command) {
    if (command == "info")
        return "Usage: pmxer-cli info [--json] MODEL\n";
    if (command == "validate")
        return "Usage: pmxer-cli validate [--json] MODEL...\n";
    if (command == "diff")
        return "Usage: pmxer-cli diff [--json] [--profile logical|preservation] LEFT RIGHT\n";
    if (command == "normalize")
        return "Usage: pmxer-cli normalize [-o OUTPUT] INPUT\n";
    return "Usage: pmxer-cli <command> [options]\n\n"
           "Commands:\n"
           "  info       Print model counts\n"
           "  validate   Validate one or more models\n"
           "  diff       Compare two models\n"
           "  normalize  Normalize weights and save a model\n"
           "  help       Show command help\n\n"
           "Options:\n"
           "  -h, --help\n"
           "      --json\n"
           "      --profile logical|preservation\n"
           "  -o, --output OUTPUT\n";
}

} // namespace pmxer
