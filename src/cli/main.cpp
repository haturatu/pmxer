#include "../editor/DiffController.hpp"
#include "../editor/DocumentSession.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/SaveController.hpp"
#include "../editor/ValidationController.hpp"
#include "../platform/Log.hpp"

#include <mmd/pmx.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: pmxer-cli <validate|info|diff|normalize> path [path]\n");
}

int validate(const std::filesystem::path &path) {
    try {
        const auto model = mmd::pmx::load(path);
        const auto result = mmd::pmx::validate(model);
        for (const auto &issue : result.issues) {
            const auto line = pmxer::validationSeverityName(issue.severity) + ": " + issue.object + ": " + issue.message;
            if (issue.severity >= mmd::ValidationSeverity::warning)
                std::fprintf(stderr, "%s\n", line.c_str());
            else
                std::printf("%s\n", line.c_str());
        }
        return result.valid() ? 0 : 1;
    } catch (const std::exception &error) {
        pmxer::log::error(error.what());
        return 1;
    }
}

int info(const std::filesystem::path &path) {
    try {
        const auto model = mmd::pmx::load(path);
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
        return 0;
    } catch (const std::exception &error) {
        pmxer::log::error(error.what());
        return 1;
    }
}

int diff(const std::filesystem::path &left, const std::filesystem::path &right) {
    try {
        const auto lhs = mmd::pmx::load(left);
        const auto rhs = mmd::pmx::load(right);
        const auto result = mmd::pmx::semanticCompare(lhs, rhs, mmd::PmxComparisonProfile::logical);
        for (const auto &line : pmxer::formatDifferences(result))
            std::puts(line.c_str());
        return result.equal() ? 0 : 2;
    } catch (const std::exception &error) {
        pmxer::log::error(error.what());
        return 1;
    }
}

int normalize(const std::filesystem::path &source, const std::filesystem::path &destination) {
    try {
        pmxer::DocumentSession session(mmd::pmx::load(source), destination);
        const auto normalized = pmxer::normalizeWeights(session);
        if (!normalized.success) {
            pmxer::log::error(normalized.message.c_str());
            return 1;
        }
        const auto result = pmxer::saveDocument(session, destination);
        if (!result.success) {
            pmxer::log::error(result.message.c_str());
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        pmxer::log::error(error.what());
        return 1;
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        usage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "validate" && argc == 3)
        return validate(argv[2]);
    if (command == "info" && argc == 3)
        return info(argv[2]);
    if (command == "diff" && argc == 4)
        return diff(argv[2], argv[3]);
    if (command == "normalize" && argc == 4)
        return normalize(argv[2], argv[3]);
    usage();
    return 1;
}
