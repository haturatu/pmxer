#include "../app/CommandLine.hpp"
#include "../editor/DiffController.hpp"
#include "../editor/DocumentSession.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/SaveController.hpp"
#include "../editor/ValidationController.hpp"
#include "../platform/Log.hpp"

#include <mmd/pmx.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

std::string jsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    for (const auto character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

int printInfo(const std::filesystem::path &path, bool json) {
    const auto model = mmd::pmx::load(path);
    if (json) {
        std::printf("{\"path\":\"%s\",\"version\":%.1f,\"vertices\":%zu,\"indices\":%zu,"
                    "\"textures\":%zu,\"materials\":%zu,\"bones\":%zu,\"morphs\":%zu,"
                    "\"display_frames\":%zu,\"rigid_bodies\":%zu,\"joints\":%zu,\"soft_bodies\":%zu}\n",
                    jsonEscape(path.string()).c_str(), model.metadata.version, model.vertices.size(), model.indices.size(),
                    model.textures.size(), model.materials.size(), model.bones.size(), model.morphs.size(),
                    model.displayFrames.size(), model.rigidBodies.size(), model.joints.size(), model.softBodies.size());
    } else {
        std::printf("version: %.1f\n", model.metadata.version);
        std::printf("vertices: %zu\n", model.vertices.size());
        std::printf("indices: %zu\n", model.indices.size());
        std::printf("textures: %zu\n", model.textures.size());
        std::printf("materials: %zu\n", model.materials.size());
        std::printf("bones: %zu\n", model.bones.size());
        std::printf("morphs: %zu\n", model.morphs.size());
        std::printf("display_frames: %zu\n", model.displayFrames.size());
        std::printf("rigid_bodies: %zu\n", model.rigidBodies.size());
        std::printf("joints: %zu\n", model.joints.size());
        std::printf("soft_bodies: %zu\n", model.softBodies.size());
    }
    return 0;
}

int printValidation(const std::filesystem::path &path, bool json) {
    const auto model = mmd::pmx::load(path);
    const auto result = mmd::pmx::validate(model);
    if (json) {
        std::printf("{\"path\":\"%s\",\"valid\":%s,\"issues\":[",
                    jsonEscape(path.string()).c_str(), result.valid() ? "true" : "false");
        for (std::size_t index = 0; index < result.issues.size(); ++index) {
            const auto &issue = result.issues[index];
            if (index != 0)
                std::printf(",");
            const auto severity = pmxer::validationSeverityName(issue.severity);
            std::printf("{\"severity\":\"%s\",\"object\":\"%s\",\"message\":\"%s\"}",
                        severity.c_str(), jsonEscape(issue.object).c_str(), jsonEscape(issue.message).c_str());
        }
        std::printf("]}\n");
    } else {
        for (const auto &issue : result.issues) {
            const auto line = pmxer::validationSeverityName(issue.severity) + ": " + issue.object + ": " + issue.message;
            if (issue.severity >= mmd::ValidationSeverity::warning)
                std::fprintf(stderr, "%s\n", line.c_str());
            else
                std::printf("%s\n", line.c_str());
        }
    }
    return result.valid() ? 0 : 1;
}

int printDiff(const std::filesystem::path &left, const std::filesystem::path &right, const pmxer::CliOptions &options) {
    const auto lhs = mmd::pmx::load(left);
    const auto rhs = mmd::pmx::load(right);
    const auto profile = options.profile == "preservation" ? mmd::PmxComparisonProfile::preservation
                                                             : mmd::PmxComparisonProfile::logical;
    const auto result = mmd::pmx::semanticCompare(lhs, rhs, profile);
    const auto differences = pmxer::formatDifferences(result);
    if (options.json) {
        std::printf("{\"equal\":%s,\"differences\":[", result.equal() ? "true" : "false");
        for (std::size_t index = 0; index < differences.size(); ++index) {
            if (index != 0)
                std::printf(",");
            std::printf("\"%s\"", jsonEscape(differences[index]).c_str());
        }
        std::printf("]}\n");
    } else {
        for (const auto &line : differences)
            std::puts(line.c_str());
    }
    return result.equal() ? 0 : 2;
}

int normalizeModel(const pmxer::CliOptions &options) {
    if (options.operands.empty() || options.operands.size() > 2)
        return 1;
    const auto &source = options.operands.front();
    const auto destination = options.output.empty()
                                 ? (options.operands.size() == 2 ? options.operands[1]
                                                                : source.parent_path() / (source.stem().string() + ".normalized.pmx"))
                                 : options.output;
    pmxer::DocumentSession session(mmd::pmx::load(source), destination);
    const auto normalized = pmxer::normalizeWeights(session);
    if (!normalized.success) {
        pmxer::log::error(normalized.message.c_str());
        return 1;
    }
    const auto saved = pmxer::saveDocument(session, destination);
    if (!saved.success) {
        pmxer::log::error(saved.message.c_str());
        return 1;
    }
    if (options.json)
        std::printf("{\"output\":\"%s\"}\n", jsonEscape(destination.string()).c_str());
    else
        std::printf("saved: %s\n", destination.string().c_str());
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const auto parsed = pmxer::parseCliArguments(argc, argv);
    if (!parsed.error.empty()) {
        pmxer::log::error(parsed.error.c_str());
        std::fputs(pmxer::cliUsage().c_str(), stderr);
        return 1;
    }
    if (parsed.options.help) {
        std::fputs(pmxer::cliUsage(parsed.options.helpCommand).c_str(), stdout);
        return 0;
    }

    try {
        const auto &options = parsed.options;
        if (options.command == "info" && options.operands.size() == 1)
            return printInfo(options.operands.front(), options.json);
        if (options.command == "validate" && !options.operands.empty()) {
            int status = 0;
            for (const auto &path : options.operands)
                status = printValidation(path, options.json) != 0 ? 1 : status;
            return status;
        }
        if (options.command == "diff" && options.operands.size() == 2)
            return printDiff(options.operands[0], options.operands[1], options);
        if (options.command == "normalize")
            return normalizeModel(options);
    } catch (const std::exception &error) {
        pmxer::log::error(error.what());
        return 1;
    }
    std::fputs(pmxer::cliUsage(parsed.options.command).c_str(), stderr);
    return 1;
}
