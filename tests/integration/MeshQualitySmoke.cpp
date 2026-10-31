#include "editor/AiEditExecutor.hpp"
#include "editor/SaveController.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mmd/pmx.hpp>
#include <numbers>
#include <numeric>
#include <set>
#include <stdexcept>

// Offline quality diagnostic for a user-supplied model. Never overwrites the
// input.
int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "Usage: pmxer_mesh_smoke INPUT.pmx OUTPUT_DIRECTORY "
                     "MATERIAL_INDEX...\n";
        return 2;
    }
    try {
        using namespace pmxer;
        using namespace pmxer::ai;
        const auto original = mmd::pmx::load(argv[1]);
        const std::filesystem::path directory = argv[2];
        if (std::filesystem::weakly_canonical(argv[1]) ==
            std::filesystem::weakly_canonical(directory / "smoothed.pmx"))
            throw std::runtime_error("Output would overwrite the input model");
        std::filesystem::create_directories(directory);
        std::set<std::size_t> materials;
        for (int i = 3; i < argc; ++i) {
            std::size_t consumed{};
            const auto index = std::stoul(argv[i], &consumed);
            if (consumed != std::string{argv[i]}.size() || index >= original.materials.size())
                throw std::runtime_error("Invalid material index");
            materials.insert(index);
        }
        DocumentSession session(original);
        std::set<mmd::MaterialHandle> materialHandles;
        for (auto i : materials)
            materialHandles.insert(session.document.materialHandle(i));
        std::vector<std::size_t> indices;
        std::map<mmd::VertexHandle, mmd::PmxVertex> outside;
        std::map<mmd::FaceHandle, mmd::PmxFace> outsideFaces;
        for (std::size_t i = 0; i < session.document.faces().size(); ++i) {
            const auto &face = session.document.faces()[i];
            if (materialHandles.contains(face.material))
                indices.push_back(i);
            else {
                outsideFaces[session.document.faceHandle(i)] = face;
                for (auto h : face.vertices)
                    outside[h] = *session.document.resolve(h);
            }
        }
        auto region = captureFaces(session, indices);
        Operation subdivision;
        subdivision.type = OperationType::subdivide;
        subdivision.iterations = 2;
        const auto subdivided = applyEditPlan(session, region, {"Resolution only", {subdivision}});
        if (!subdivided.success)
            throw std::runtime_error(subdivided.message);
        const auto refined = session.document.model();
        const auto capture = [](DocumentSession &s) {
            std::vector<std::size_t> all(s.document.faces().size());
            std::iota(all.begin(), all.end(), std::size_t{0});
            return captureFaces(s, all);
        };
        Operation smoothing;
        smoothing.type = OperationType::smooth;
        smoothing.amount = 0;
        DocumentSession zero(refined);
        const auto noop = applyEditPlan(zero, capture(zero), {"Zero strength", {smoothing}});
        if (!noop.success)
            throw std::runtime_error(noop.message);
        const bool zeroPreserved =
            mmd::pmx::semanticCompare(refined, zero.document.model()).equal();
        std::vector<std::size_t> targetIndices;
        for (std::size_t i = 0; i < session.document.faces().size(); ++i)
            if (materialHandles.contains(session.document.faces()[i].material))
                targetIndices.push_back(i);
        smoothing.amount = 0.2F;
        const auto result = applyEditPlan(session, captureFaces(session, targetIndices),
                                          {"Gentle smoothing", {smoothing}});
        if (!result.success)
            throw std::runtime_error(result.message);
        const auto edited = session.document.model();
        std::vector<double> angles;
        double maxDisplacement{};
        for (std::size_t i = 0; i < refined.vertices.size(); ++i) {
            const auto &a = refined.vertices[i];
            const auto &b = edited.vertices[i];
            double dot{}, aLength{}, bLength{}, distance{};
            for (std::size_t j = 0; j < 3; ++j) {
                dot += static_cast<double>(a.normal[j]) * b.normal[j];
                aLength += static_cast<double>(a.normal[j]) * a.normal[j];
                bLength += static_cast<double>(b.normal[j]) * b.normal[j];
                const double delta = b.position[j] - a.position[j];
                distance += delta * delta;
            }
            if (aLength > 1e-12 && bLength > 1e-12)
                angles.push_back(
                    std::acos(std::clamp(dot / std::sqrt(aLength * bLength), -1.0, 1.0)) * 180 /
                    std::numbers::pi);
            maxDisplacement = std::max(maxDisplacement, std::sqrt(distance));
        }
        std::sort(angles.begin(), angles.end());
        if (angles.empty())
            throw std::runtime_error("No valid source normals");
        for (const auto &[h, v] : outside)
            if (!session.document.resolve(h) || *session.document.resolve(h) != v)
                throw std::runtime_error("Outside vertex changed");
        for (const auto &[h, face] : outsideFaces)
            if (!session.document.resolve(h) || *session.document.resolve(h) != face)
                throw std::runtime_error("Outside face changed");
        if (!session.undo() ||
            !mmd::pmx::semanticCompare(refined, session.document.model()).equal() ||
            !session.redo() || !mmd::pmx::semanticCompare(edited, session.document.model()).equal())
            throw std::runtime_error("Undo/redo mismatch");
        const auto saved = saveDocument(session, directory / "smoothed.pmx");
        if (!saved.success ||
            !mmd::pmx::semanticCompare(edited, mmd::pmx::load(directory / "smoothed.pmx")).equal())
            throw std::runtime_error("Save/reload mismatch");
        nlohmann::json report = {{"inputVertices", original.vertices.size()},
                                 {"outputVertices", edited.vertices.size()},
                                 {"outputFaces", edited.indices.size() / 3},
                                 {"zeroStrengthPreservesModel", zeroPreserved},
                                 {"normalAngleP95Degrees", angles[angles.size() * 95 / 100]},
                                 {"normalAngleMaxDegrees", angles.back()},
                                 {"maxDisplacement", maxDisplacement},
                                 {"outsidePreserved", true},
                                 {"saveReloadUndoRedo", true}};
        std::ofstream(directory / "quality.json") << report.dump(2) << '\n';
        std::cout << "INFO " << report.dump() << '\n';
        if (!zeroPreserved)
            throw std::runtime_error("Zero-strength smoothing changed shading");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
