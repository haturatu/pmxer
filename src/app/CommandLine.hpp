#pragma once

#include <mmd/pmx.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pmxer {

enum class OutputFormat { text, json };

struct GlobalOptions {
    bool version{};
};

struct EditCommand {
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

struct InfoCommand {
    std::filesystem::path model;
    OutputFormat format{OutputFormat::text};
};

struct ValidateCommand {
    std::vector<std::filesystem::path> models;
    OutputFormat format{OutputFormat::text};
};

struct DiffCommand {
    std::filesystem::path left;
    std::filesystem::path right;
    mmd::PmxComparisonProfile profile{mmd::PmxComparisonProfile::logical};
    OutputFormat format{OutputFormat::text};
};

struct NormalizeCommand {
    std::filesystem::path input;
    std::filesystem::path output;
    OutputFormat format{OutputFormat::text};
};

struct HelpCommand {
    std::string command;
};

using Command = std::variant<EditCommand, InfoCommand, ValidateCommand, DiffCommand, NormalizeCommand, HelpCommand>;

struct Invocation {
    GlobalOptions global;
    Command command;
};

struct ParseResult {
    std::optional<Invocation> invocation;
    std::string error;
};

[[nodiscard]] ParseResult parseInvocation(int argc, char *const argv[]);
[[nodiscard]] std::string usage(std::string_view command = {});
[[nodiscard]] std::string applicationVersion();

} // namespace pmxer
