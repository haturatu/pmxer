#include "CommandOutput.hpp"

#include "../editor/DiffController.hpp"
#include "../editor/DocumentSession.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/SaveController.hpp"
#include "../editor/ValidationController.hpp"
#include "../platform/Log.hpp"

#include <mmd/pmx.hpp>

#include <cstdio>
#include <string_view>

namespace pmxer {
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

bool isJson(OutputFormat format) {
    return format == OutputFormat::json;
}

} // namespace

int runInfoCommand(const InfoCommand &command) {
    const auto model = mmd::pmx::load(command.model);
    if (isJson(command.format)) {
        std::printf("{\"path\":\"%s\",\"version\":%.1f,\"vertices\":%zu,\"indices\":%zu,"
                    "\"textures\":%zu,\"materials\":%zu,\"bones\":%zu,\"morphs\":%zu,"
                    "\"display_frames\":%zu,\"rigid_bodies\":%zu,\"joints\":%zu,\"soft_bodies\":%zu}\n",
                    jsonEscape(command.model.string()).c_str(), model.metadata.version, model.vertices.size(),
                    model.indices.size(), model.textures.size(), model.materials.size(), model.bones.size(),
                    model.morphs.size(), model.displayFrames.size(), model.rigidBodies.size(), model.joints.size(),
                    model.softBodies.size());
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

int runValidateCommand(const ValidateCommand &command) {
    int status = 0;
    for (const auto &path : command.models) {
        const auto model = mmd::pmx::load(path);
        const auto result = mmd::pmx::validate(model);
        if (isJson(command.format)) {
            std::printf("{\"path\":\"%s\",\"valid\":%s,\"issues\":[",
                        jsonEscape(path.string()).c_str(), result.valid() ? "true" : "false");
            for (std::size_t index = 0; index < result.issues.size(); ++index) {
                if (index != 0)
                    std::printf(",");
                const auto &issue = result.issues[index];
                const auto severity = validationSeverityName(issue.severity);
                std::printf("{\"severity\":\"%s\",\"object\":\"%s\",\"message\":\"%s\"}",
                            severity.c_str(), jsonEscape(issue.object).c_str(), jsonEscape(issue.message).c_str());
            }
            std::printf("]}\n");
        } else {
            for (const auto &issue : result.issues) {
                const auto line = validationSeverityName(issue.severity) + ": " + issue.object + ": " + issue.message;
                if (issue.severity >= mmd::ValidationSeverity::warning)
                    std::fprintf(stderr, "%s\n", line.c_str());
                else
                    std::printf("%s\n", line.c_str());
            }
        }
        if (!result.valid())
            status = 1;
    }
    return status;
}

int runDiffCommand(const DiffCommand &command) {
    const auto lhs = mmd::pmx::load(command.left);
    const auto rhs = mmd::pmx::load(command.right);
    const auto result = mmd::pmx::semanticCompare(lhs, rhs, command.profile);
    const auto differences = formatDifferences(result);
    if (isJson(command.format)) {
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
    return result.equal() ? 0 : 1;
}

int runNormalizeCommand(const NormalizeCommand &command) {
    const auto destination = command.output.empty()
                                 ? command.input.parent_path() / (command.input.stem().string() + ".normalized.pmx")
                                 : command.output;
    DocumentSession session(mmd::pmx::load(command.input), destination);
    const auto normalized = normalizeWeights(session);
    if (!normalized.success) {
        log::error(normalized.message.c_str());
        return 3;
    }
    const auto saved = saveDocument(session, destination);
    if (!saved.success) {
        log::error(saved.message.c_str());
        return 3;
    }
    if (isJson(command.format))
        std::printf("{\"output\":\"%s\"}\n", jsonEscape(destination.string()).c_str());
    else
        std::printf("saved: %s\n", destination.string().c_str());
    return 0;
}

} // namespace pmxer
