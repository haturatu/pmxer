#include "CommandLine.hpp"

#include <argparse/argparse.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace pmxer {
namespace {

using Parser = argparse::ArgumentParser;

void addHelp(Parser &parser, const char *description) {
    parser.add_argument("-h", "--help")
        .help(description)
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
}

void addJson(Parser &parser) {
    parser.add_argument("--json")
        .help("write machine-readable JSON")
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
}

void configureEditParser(Parser &parser) {
    addHelp(parser, "show this help message");
    parser.add_argument("--gpu-debug")
        .help("enable GPU validation")
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
    parser.add_argument("--no-physics")
        .help("disable physics preview")
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
    parser.add_argument("--safe-mode")
        .help("use conservative editor settings")
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
    parser.add_argument("--renderer")
        .help("select the graphics backend")
        .default_value(std::string{"auto"})
        .choices("auto", "vulkan", "direct3d12", "metal");
    parser.add_argument("--font")
        .help("path to the interface font")
        .default_value(std::string{});
    parser.add_argument("--font-size")
        .help("interface font size")
        .default_value(18.0F)
        .scan<'g', float>();
    parser.add_argument("--lang")
        .help("interface locale")
        .default_value(std::string{"auto"});
    parser.add_argument("--resource-dir")
        .help("directory containing application resources")
        .default_value(std::string{});
    parser.add_argument("documents")
        .help("PMX files to open")
        .nargs(argparse::nargs_pattern::any);
}

void configureInfoParser(Parser &parser) {
    addHelp(parser, "show this help message");
    addJson(parser);
    parser.add_argument("model").help("PMX file");
}

void configureValidateParser(Parser &parser) {
    addHelp(parser, "show this help message");
    addJson(parser);
    parser.add_argument("models")
        .help("PMX files to validate")
        .nargs(argparse::nargs_pattern::at_least_one);
}

void configureDiffParser(Parser &parser) {
    addHelp(parser, "show this help message");
    addJson(parser);
    parser.add_argument("--profile")
        .help("comparison profile")
        .default_value(std::string{"logical"})
        .choices("logical", "preservation");
    parser.add_argument("left").help("left PMX file");
    parser.add_argument("right").help("right PMX file");
}

void configureNormalizeParser(Parser &parser) {
    addHelp(parser, "show this help message");
    addJson(parser);
    parser.add_argument("-o", "--output")
        .help("output PMX file")
        .default_value(std::string{});
    parser.add_argument("input").help("input PMX file");
}

void configureHelpParser(Parser &parser) {
    addHelp(parser, "show this help message");
    parser.add_argument("command")
        .help("command to describe")
        .nargs(argparse::nargs_pattern::optional);
}

std::vector<std::string> tailArguments(int argc, char *const argv[], int first) {
    std::vector<std::string> arguments{"pmxer"};
    for (int index = first; index < argc; ++index) {
        arguments.emplace_back(argv[index] == nullptr ? "" : argv[index]);
    }
    return arguments;
}

bool hasHelpArgument(int argc, char *const argv[], int first) {
    for (int index = first; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--") {
            return false;
        }
        if (argument == "-h" || argument == "--help") {
            return true;
        }
    }
    return false;
}

void rejectEmptyOptionValues(int argc, char *const argv[], int first) {
    constexpr const char *options[] = {
        "--renderer=", "--font=", "--font-size=", "--lang=", "--resource-dir=",
    };
    for (int index = first; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--") {
            return;
        }
        for (const char *option : options) {
            if (argument == option) {
                throw std::runtime_error(std::string(option).substr(0, std::string(option).size() - 1) +
                                         " requires a value");
            }
        }
    }
}

template <typename Function>
ParseResult parseWith(Parser &parser, Function &&function) {
    try {
        return {function(parser), {}};
    } catch (const std::exception &error) {
        return {std::nullopt, error.what()};
    }
}

ParseResult parseEdit(int argc, char *const argv[], int first) {
    Parser parser("pmxer edit", applicationVersion(), argparse::default_arguments::none, false);
    configureEditParser(parser);
    return parseWith(parser, [argc, argv, first](Parser &parser) -> std::optional<Invocation> {
        if (hasHelpArgument(argc, argv, first)) {
            return Invocation{{}, HelpCommand{"edit"}};
        }
        rejectEmptyOptionValues(argc, argv, first);
        auto arguments = tailArguments(argc, argv, first);
        std::vector<std::string> positionalOnly;
        const auto separator = std::find(arguments.begin() + 1, arguments.end(), "--");
        if (separator != arguments.end()) {
            positionalOnly.assign(std::next(separator), arguments.end());
            arguments.erase(separator, arguments.end());
        }
        parser.parse_args(arguments);

        EditCommand command;
        command.gpuDebug = parser.get<bool>("--gpu-debug");
        command.safeMode = parser.get<bool>("--safe-mode");
        command.physics = !parser.get<bool>("--no-physics") && !command.safeMode;
        command.renderer = parser.get<std::string>("--renderer");
        command.locale = parser.get<std::string>("--lang");
        command.fontSize = parser.get<float>("--font-size");
        if (!std::isfinite(command.fontSize) || command.fontSize < 6.0F || command.fontSize > 256.0F) {
            throw std::runtime_error("font size must be between 6 and 256");
        }
        command.font = parser.get<std::string>("--font");
        command.resourceDirectory = parser.get<std::string>("--resource-dir");
        for (const auto &document : parser.get<std::vector<std::string>>("documents")) {
            command.documents.emplace_back(document);
        }
        for (const auto &document : positionalOnly) {
            command.documents.emplace_back(document);
        }
        return Invocation{{}, std::move(command)};
    });
}

ParseResult parseInfo(int argc, char *const argv[], int first) {
    Parser parser("pmxer info", applicationVersion(), argparse::default_arguments::none, false);
    configureInfoParser(parser);
    return parseWith(parser, [argc, argv, first](Parser &parser) -> std::optional<Invocation> {
        if (hasHelpArgument(argc, argv, first)) {
            return Invocation{{}, HelpCommand{"info"}};
        }
        parser.parse_args(tailArguments(argc, argv, first));
        InfoCommand command;
        command.model = parser.get<std::string>("model");
        command.format = parser.get<bool>("--json") ? OutputFormat::json : OutputFormat::text;
        return Invocation{{}, std::move(command)};
    });
}

ParseResult parseValidate(int argc, char *const argv[], int first) {
    Parser parser("pmxer validate", applicationVersion(), argparse::default_arguments::none, false);
    configureValidateParser(parser);
    return parseWith(parser, [argc, argv, first](Parser &parser) -> std::optional<Invocation> {
        if (hasHelpArgument(argc, argv, first)) {
            return Invocation{{}, HelpCommand{"validate"}};
        }
        parser.parse_args(tailArguments(argc, argv, first));
        ValidateCommand command;
        command.format = parser.get<bool>("--json") ? OutputFormat::json : OutputFormat::text;
        for (const auto &model : parser.get<std::vector<std::string>>("models")) {
            command.models.emplace_back(model);
        }
        return Invocation{{}, std::move(command)};
    });
}

ParseResult parseDiff(int argc, char *const argv[], int first) {
    Parser parser("pmxer diff", applicationVersion(), argparse::default_arguments::none, false);
    configureDiffParser(parser);
    return parseWith(parser, [argc, argv, first](Parser &parser) -> std::optional<Invocation> {
        if (hasHelpArgument(argc, argv, first)) {
            return Invocation{{}, HelpCommand{"diff"}};
        }
        parser.parse_args(tailArguments(argc, argv, first));
        DiffCommand command;
        command.left = parser.get<std::string>("left");
        command.right = parser.get<std::string>("right");
        command.format = parser.get<bool>("--json") ? OutputFormat::json : OutputFormat::text;
        const auto profile = parser.get<std::string>("--profile");
        command.profile = profile == "preservation"
            ? mmd::PmxComparisonProfile::preservation
            : mmd::PmxComparisonProfile::logical;
        return Invocation{{}, std::move(command)};
    });
}

ParseResult parseNormalize(int argc, char *const argv[], int first) {
    Parser parser("pmxer normalize", applicationVersion(), argparse::default_arguments::none, false);
    configureNormalizeParser(parser);
    return parseWith(parser, [argc, argv, first](Parser &parser) -> std::optional<Invocation> {
        if (hasHelpArgument(argc, argv, first)) {
            return Invocation{{}, HelpCommand{"normalize"}};
        }
        parser.parse_args(tailArguments(argc, argv, first));
        NormalizeCommand command;
        command.input = parser.get<std::string>("input");
        command.output = parser.get<std::string>("--output");
        command.format = parser.get<bool>("--json") ? OutputFormat::json : OutputFormat::text;
        return Invocation{{}, std::move(command)};
    });
}

ParseResult parseHelp(int argc, char *const argv[], int first) {
    Parser parser("pmxer help", applicationVersion(), argparse::default_arguments::none, false);
    configureHelpParser(parser);
    return parseWith(parser, [argc, argv, first](Parser &parser) -> std::optional<Invocation> {
        if (hasHelpArgument(argc, argv, first)) {
            return Invocation{{}, HelpCommand{}};
        }
        parser.parse_args(tailArguments(argc, argv, first));
        HelpCommand command;
        if (parser.is_used("command")) {
            command.command = parser.get<std::string>("command");
        }
        return Invocation{{}, std::move(command)};
    });
}

} // namespace

ParseResult parseInvocation(int argc, char *const argv[]) {
    if (argc <= 1) {
        return ParseResult{Invocation{{}, EditCommand{}}, {}};
    }

    const std::string first = argv[1] == nullptr ? "" : argv[1];
    if (first == "-v" || first == "--version") {
        Invocation invocation{{}, EditCommand{}};
        invocation.global.version = true;
        return ParseResult{std::move(invocation), {}};
    }
    if (first == "-h" || first == "--help") {
        return ParseResult{Invocation{{}, HelpCommand{}}, {}};
    }
    if (first == "--") {
        return parseEdit(argc, argv, 1);
    }
    if (first == "edit") {
        return parseEdit(argc, argv, 2);
    }
    if (first == "info") {
        return parseInfo(argc, argv, 2);
    }
    if (first == "validate") {
        return parseValidate(argc, argv, 2);
    }
    if (first == "diff") {
        return parseDiff(argc, argv, 2);
    }
    if (first == "normalize") {
        return parseNormalize(argc, argv, 2);
    }
    if (first == "help") {
        return parseHelp(argc, argv, 2);
    }
    if (!first.empty() && first.front() == '-') {
        return parseEdit(argc, argv, 1);
    }
    return parseEdit(argc, argv, 1);
}

std::string usage(std::string_view command) {
    if (command == "edit") {
        Parser parser("pmxer edit", applicationVersion(), argparse::default_arguments::none, false);
        configureEditParser(parser);
        return parser.help().str();
    }
    if (command == "info") {
        Parser parser("pmxer info", applicationVersion(), argparse::default_arguments::none, false);
        configureInfoParser(parser);
        return parser.help().str();
    }
    if (command == "validate") {
        Parser parser("pmxer validate", applicationVersion(), argparse::default_arguments::none, false);
        configureValidateParser(parser);
        return parser.help().str();
    }
    if (command == "diff") {
        Parser parser("pmxer diff", applicationVersion(), argparse::default_arguments::none, false);
        configureDiffParser(parser);
        return parser.help().str();
    }
    if (command == "normalize") {
        Parser parser("pmxer normalize", applicationVersion(), argparse::default_arguments::none, false);
        configureNormalizeParser(parser);
        return parser.help().str();
    }
    if (command == "help") {
        Parser parser("pmxer help", applicationVersion(), argparse::default_arguments::none, false);
        configureHelpParser(parser);
        return parser.help().str();
    }
    return "Usage:\n"
           "  pmxer [options] [FILE...]\n"
           "  pmxer <command> [options] ...\n\n"
           "Commands:\n"
           "  edit        Open PMX files in the editor\n"
           "  info        Display model information\n"
           "  validate    Validate PMX files\n"
           "  diff        Compare PMX models\n"
           "  normalize   Normalize model weights\n"
           "  help        Show command help\n\n"
           "Use: pmxer help <command>\n";
}

std::string applicationVersion() {
    return "0.1.0";
}

} // namespace pmxer
