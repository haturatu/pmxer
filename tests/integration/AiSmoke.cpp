#include "ai/CodexClient.hpp"
#include "editor/AiEditExecutor.hpp"
#include "editor/SaveController.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mmd/pmx.hpp>
#include <stdexcept>
#include <thread>

int main(int argc, char **argv) {
    if (argc != 4 ||
        (std::string_view{argv[1]} != "--live" && std::string_view{argv[1]} != "--replay")) {
        std::cerr << "Usage: pmxer_ai_smoke --live|--replay INPUT.pmx OUTPUT_DIRECTORY\nUses the "
                     "existing Codex ChatGPT account to generate three edit plans.\n";
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
        for (const auto &scenario :
             {std::string{"loafers"}, std::string{"shirt"}, std::string{"skirt"}}) {
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
                if (inside)
                    faces.push_back(face);
            }
            if (faces.empty())
                throw std::runtime_error("Empty smoke selection");
            const auto region = pmxer::ai::captureFaces(session, faces);
            const std::string prompt =
                scenario == "loafers" ? "この足元を黒いローファーの形状に置き換えてください。元の選"
                                        "択面を置き換え、プロシージャル生成してください。"
                : scenario == "shirt" ? "選択した服を白い半袖シャツの形状に変更してください。元の選"
                                        "択面を置き換え、プロシージャル生成してください。"
                                      : "選択範囲を紺色のプリーツスカートの形状に変更してください。"
                                        "元の選択面を置き換え、プロシージャル生成してください。";
            std::optional<std::string> text;
            if (std::string_view{argv[1]} == "--live") {
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
            } else {
                std::ifstream file(directory / (scenario + ".json"));
                if (!file)
                    throw std::runtime_error("No recorded plan");
                text = std::string{std::istreambuf_iterator<char>{file},
                                   std::istreambuf_iterator<char>{}};
            }
            auto plan = pmxer::ai::parsePlan(*text);
            const auto expected = scenario == "loafers" ? pmxer::ai::Shape::loafers
                                  : scenario == "shirt" ? pmxer::ai::Shape::shirt
                                                        : pmxer::ai::Shape::skirt;
            if (plan.operations.size() != 1 ||
                plan.operations[0].type != pmxer::ai::OperationType::generate ||
                plan.operations[0].shape != expected)
                throw std::runtime_error("Live plan did not generate the requested shape");
            {
                std::ofstream file(directory / (scenario + ".json"));
                file << *text;
            }
            const auto result = pmxer::ai::applyEditPlan(session, region, plan);
            if (!result.success)
                throw std::runtime_error(result.message);
            const auto edited = session.document.model();
            const auto saved = pmxer::saveDocument(session, directory / (scenario + ".pmx"));
            if (!saved.success)
                throw std::runtime_error(saved.message);
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
