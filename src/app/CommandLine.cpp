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

} // namespace pmxer
