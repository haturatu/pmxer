#include "ai/CodexClient.hpp"
#include "editor/AiEditExecutor.hpp"
#include "editor/SaveController.hpp"
#include "editor/TailoredGarment.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mmd/pmx.hpp>
#include <set>
#include <stdexcept>
#include <thread>

int main(int argc, char **argv) {
    if ((argc != 4 && argc != 5) ||
        (std::string_view{argv[1]} != "--live" && std::string_view{argv[1]} != "--replay" &&
         std::string_view{argv[1]} != "--broad-live" &&
         std::string_view{argv[1]} != "--broad-replay" &&
         std::string_view{argv[1]} != "--local-outfit")) {
        std::cerr
            << "Usage: pmxer_ai_smoke --live|--replay|--broad-live|--broad-replay|--local-outfit "
               "INPUT.pmx "
               "OUTPUT_DIRECTORY [SCENARIO]\nUses the "
               "existing Codex ChatGPT account for live plans; --local-outfit works offline.\n";
        return 2;
    }
    try {
        const auto original = mmd::pmx::load(argv[2]);
        if (original.vertices.empty())
            throw std::runtime_error("The smoke model has no vertices");
        const std::filesystem::path directory = argv[3];
        std::filesystem::create_directories(directory);
        float minY = original.vertices[0].position[1], maxY = minY;
        for (const auto &v : original.vertices) {
            minY = std::min(minY, v.position[1]);
            maxY = std::max(maxY, v.position[1]);
        }
        const auto height = maxY - minY;
        if (height < 1e-5F)
            throw std::runtime_error("The smoke model has no vertical extent");
        const bool local = std::string_view{argv[1]} == "--local-outfit";
        const bool broad = local || std::string_view{argv[1]}.starts_with("--broad-");
        const std::vector<std::string> scenarios =
            local   ? std::vector<std::string>{"gothic"}
            : broad ? std::vector<std::string>{"rough-loafers", "dress", "pants",
                                               "upscale",       "hair",  "gothic"}
                    : std::vector<std::string>{"loafers", "shirt", "skirt"};
        if (argc == 5 && std::find(scenarios.begin(), scenarios.end(), argv[4]) == scenarios.end())
            throw std::runtime_error("Unknown smoke scenario");
        for (const auto &scenario : scenarios) {
            if (argc == 5 && scenario != argv[4])
                continue;
            pmxer::DocumentSession session(original);
            std::vector<std::size_t> faces;
            for (std::size_t face = 0; face < original.indices.size() / 3; ++face) {
                float y = 0, x = 0;
                for (std::size_t k = 0; k < 3; ++k) {
                    const auto &p = original.vertices[original.indices[face * 3 + k]].position;
                    y += p[1] / 3;
                    x += p[0] / 3;
                }
                const auto relative = (y - minY) / height;
                const bool inside = scenario == "loafers" ? relative < 0.13F
                                    : scenario == "shirt" ? (relative > 0.55F && relative < 0.82F &&
                                                             std::abs(x) < height * 0.16F)
                                                          : relative > 0.38F && relative < 0.59F;
                if (broad || inside)
                    faces.push_back(face);
            }
            if (faces.empty())
                throw std::runtime_error("Empty smoke selection");
            auto region = pmxer::ai::captureFaces(session, faces);
            region.allowMaterialExpansion = broad;
            const std::string prompt =
                scenario == "gothic" ? "ゴスロリ風の服にして。顔、髪、肌は変えないで。"
                : scenario == "rough-loafers"
                    ? "顔や髪、スカートも入るくらいざっくり囲みました。この中の靴だけ黒いローファー"
                      "に変えて。他の部分は変えないで。"
                : scenario == "dress" ? "この中の服装だけを青いワンピースに変えて。顔・髪・肌・靴は"
                                        "保持して。基本形状でよいです。"
                : scenario == "pants"
                    ? "スカートを黒い長ズボンの基本形状に置き換えて。顔や髪や肌は変えないで。"
                : scenario == "upscale" ? "モデル全体のメッシュを1段階アップスケールして高密度化し"
                                          "てください。テクスチャや大きさはそのままで。"
                : scenario == "hair"    ? "髪だけ縦方向に1.15倍に伸ばして、顔や衣装は変えないで。"
                : scenario == "loafers"
                    ? "この足元を黒いローファーの形状に置き換えてください。元の選"
                      "択面を置き換え、プロシージャル生成してください。"
                : scenario == "shirt" ? "選択した服を白い半袖シャツの形状に変更してください。元の選"
                                        "択面を置き換え、プロシージャル生成してください。"
                                      : "選択範囲を紺色のプリーツスカートの形状に変更してください。"
                                        "元の選択面を置き換え、プロシージャル生成してください。";
            std::optional<std::string> text;
            if (std::string_view{argv[1]} == "--live" ||
                std::string_view{argv[1]} == "--broad-live") {
                std::cout << "INFO requesting " << scenario << std::endl;
                pmxer::ai::CodexClient client;
                client.generate(prompt, pmxer::ai::regionContext(session, region).dump());
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(4);
                while (client.state().busy && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (client.state().busy) {
                    client.cancel();
                    throw std::runtime_error("Live generation timed out");
                }
                if (!client.state().error.empty())
                    throw std::runtime_error(client.state().error);
                text = client.takePlan();
                if (!text)
                    throw std::runtime_error("No live plan");
            } else if (!local) {
                std::ifstream file(directory / (scenario + ".json"));
                if (!file)
                    throw std::runtime_error("No recorded plan");
                text = std::string{std::istreambuf_iterator<char>{file},
                                   std::istreambuf_iterator<char>{}};
            }
            pmxer::ai::EditPlan plan;
            if (local) {
                pmxer::ai::Operation op;
                op.type = pmxer::ai::OperationType::generate;
                op.shape = pmxer::ai::Shape::dress;
                op.color = {0.045F, 0.035F, 0.055F, 1};
                op.garment.style = pmxer::ai::GarmentStyle::gothicLolita;
                plan = {"Local Gothic Lolita construction",
                        {op},
                        pmxer::ai::suggestOutfitTarget(session, region)};
            } else
                plan = pmxer::ai::parsePlan(*text);
            const auto expected = scenario == "loafers" ? pmxer::ai::Shape::loafers
                                  : scenario == "shirt" ? pmxer::ai::Shape::shirt
                                                        : pmxer::ai::Shape::skirt;
            if (!broad && (plan.operations.size() != 1 ||
                           plan.operations[0].type != pmxer::ai::OperationType::generate ||
                           plan.operations[0].shape != expected))
                throw std::runtime_error("Live plan did not generate the requested shape");
            if (text) {
                std::ofstream file(directory / (scenario + ".json"));
                file << *text;
            }
            if (plan.operations.empty())
                throw std::runtime_error("Requested edit was refused: " + plan.summary);
            if (broad) {
                const auto type = scenario == "upscale" ? pmxer::ai::OperationType::subdivide
                                  : scenario == "hair"  ? pmxer::ai::OperationType::transform
                                                        : pmxer::ai::OperationType::generate;
                const auto shape = scenario == "rough-loafers" ? pmxer::ai::Shape::loafers
                                   : (scenario == "dress" || scenario == "gothic")
                                       ? pmxer::ai::Shape::dress
                                       : pmxer::ai::Shape::pants;
                if (plan.operations[0].type != type ||
                    (type == pmxer::ai::OperationType::generate &&
                     plan.operations[0].shape != shape))
                    throw std::runtime_error(
                        "Broad request did not produce the expected operation");
            }
            if (scenario == "gothic" &&
                plan.operations.front().garment.style != pmxer::ai::GarmentStyle::gothicLolita)
                throw std::runtime_error("Gothic outfit request was reduced to a plain dress");
            if (scenario == "upscale" &&
                std::any_of(plan.operations.begin(), plan.operations.end(), [](const auto &op) {
                    return op.type != pmxer::ai::OperationType::subdivide;
                }))
                throw std::runtime_error("Resolution-only request changed the surface shape");
            const auto target = pmxer::ai::resolveTarget(session, region, plan.target);
            const std::set<mmd::FaceHandle> targets(target.faces.begin(), target.faces.end());
            std::map<mmd::FaceHandle, mmd::PmxFace> untouched;
            std::map<mmd::VertexHandle, mmd::PmxVertex> untouchedVertices;
            for (std::size_t i = 0; i < session.document.faces().size(); ++i) {
                const auto h = session.document.faceHandle(i);
                if (targets.contains(h))
                    continue;
                const auto face = *session.document.resolve(h);
                untouched[h] = face;
                for (auto v : face.vertices)
                    untouchedVertices[v] = *session.document.resolve(v);
            }
            std::cout << "INFO " << scenario << " target=" << target.faces.size() << ": "
                      << plan.summary << std::endl;
            const auto editStarted = std::chrono::steady_clock::now();
            const auto result = pmxer::ai::applyEditPlan(session, region, plan);
            if (!result.success)
                throw std::runtime_error(result.message);
            std::cout << "INFO mesh edit time: "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - editStarted)
                             .count()
                      << " ms" << std::endl;
            for (const auto &[h, face] : untouched)
                if (!session.document.resolve(h) || *session.document.resolve(h) != face)
                    throw std::runtime_error("A face outside the target changed");
            for (const auto &[h, vertex] : untouchedVertices)
                if (!session.document.resolve(h) || *session.document.resolve(h) != vertex)
                    throw std::runtime_error("A vertex outside the target changed");
            const auto edited = session.document.model();
            if (scenario == "gothic") {
                auto parts = nlohmann::json::array();
                for (const auto &material : edited.materials)
                    if (material.name.starts_with("AI Gothic"))
                        parts.push_back(
                            {{"name", material.name}, {"triangles", material.indexCount / 3}});
                std::ofstream report(directory / "gothic-parts.json");
                report << parts.dump(2);
                if (parts.size() < 7)
                    throw std::runtime_error("Gothic outfit lacks separate garment components");
            }
            const auto saved = pmxer::saveDocument(session, directory / (scenario + ".pmx"));
            if (!saved.success) {
                for (const auto &difference : saved.verification.differences)
                    std::cerr << "ERROR verification " << difference.path << ": "
                              << difference.message << '\n';
                throw std::runtime_error(saved.message);
            }
            if (!mmd::pmx::semanticCompare(edited, mmd::pmx::load(directory / (scenario + ".pmx")))
                     .equal())
                throw std::runtime_error("Round-trip mismatch");
            if (!session.undo() ||
                !mmd::pmx::semanticCompare(original, session.document.model()).equal())
                throw std::runtime_error("Live edit undo mismatch");
            if (!session.redo() ||
                !mmd::pmx::semanticCompare(edited, session.document.model()).equal())
                throw std::runtime_error("Live edit redo mismatch");
            std::cout << "INFO " << scenario << ": selected " << region.faces.size()
                      << " faces; output " << edited.vertices.size() << " vertices, "
                      << edited.indices.size() / 3
                      << " faces; saved, reloaded, undo/redo verified\n"
                      << std::flush;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
