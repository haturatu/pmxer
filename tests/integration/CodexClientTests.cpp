#include "ai/CodexClient.hpp"
#include "ai/EditPlan.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using Json = nlohmann::json;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void environment(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
    require(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), key, value, true),
            "Could not configure mock app-server environment");
}
void wait(pmxer::ai::CodexClient &client, const char *phase) {
    std::cout << "INFO " << phase << std::endl;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (client.state().busy && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (client.state().busy)
        std::cerr << "ERROR Codex client timed out during " << phase << std::endl;
    require(!client.state().busy, "Codex client did not finish");
}
int server() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = Json::parse(line);
        if (!request.contains("id") || !request.contains("method"))
            continue;
        const auto method = request.at("method").get<std::string>();
        Json result = Json::object();
        if (method == "account/read") {
            const auto *mode = std::getenv("PMXER_TEST_CODEX_ACCOUNT");
            result = {
                {"account",
                 {{"type", mode && *mode ? mode : "chatgpt"}, {"email", "test@example.invalid"}}}};
        }
        if (method == "thread/start") {
            const auto &p = request.at("params");
            require(p["sandbox"] == "read-only" && p["approvalPolicy"] == "never" &&
                        p["ephemeral"] == true,
                    "Unsafe planner settings");
            require(p["config"]["mcp_servers"].empty() &&
                        p["config"]["features.apps"] == false &&
                        p["config"]["features.shell_tool"] == false &&
                        p["config"]["features.multi_agent"] == false,
                    "Planner exposed external tools");
            result = {{"thread", {{"id", "thread-test"}}}};
        }
        if (method == "turn/start")
            result = {{"turn", {{"id", "turn-test"}, {"status", "inProgress"}}}};
        std::cout << Json{{"id", request["id"]}, {"result", result}}.dump() << std::endl;
        if (method == "turn/start") {
            const auto text = request["params"]["input"][0]["text"].get<std::string>();
            if (text.find("wait forever") != std::string::npos)
                continue;
            if (text.find("request tool") != std::string::npos) {
                std::cout << Json{{"id", 500},
                                  {"method", "item/commandExecution/requestApproval"},
                                  {"params", Json::object()}}
                                 .dump()
                          << std::endl;
                continue;
            }
            std::string answer =
                R"({"version":1,"summary":"Mock color edit","operations":[{"type":"recolor","shape":"none","color":[0,0,0,1],"translation":[0,0,0],"scale":[1,1,1],"rotation":[0,0,0],"replace":false,"detail":24}]})";
            if (text.find("invalid response") != std::string::npos)
                answer = "not json";
            std::cout << Json{{"method", "item/completed"},
                              {"params",
                               {{"threadId", "thread-test"},
                                {"item", {{"type", "agentMessage"}, {"text", answer}}}}}}
                             .dump()
                      << std::endl;
            std::cout << Json{{"method", "turn/completed"},
                              {"params",
                               {{"threadId", "thread-test"},
                                {"turn",
                                 {{"id", "turn-test"},
                                  {"status", text.find("failed turn") == std::string::npos
                                                 ? "completed"
                                                 : "failed"}}}}}}
                             .dump()
                      << std::endl;
        }
    }
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view{argv[1]} == "app-server")
            return server();
        const auto self = std::filesystem::absolute(argv[0]).string();
        environment("PMXER_CODEX", self.c_str());
        environment("PMXER_TEST_CODEX_ACCOUNT", "chatgpt");
        pmxer::ai::CodexClient client;
        client.connect(false);
        wait(client, "read account");
        require(client.state().connected, "Account read failed");
        client.generate("black color", "{}");
        wait(client, "generate plan");
        require(client.state().error.empty(), client.state().error.c_str());
        auto plan = client.takePlan();
        require(plan.has_value(), "Missing structured plan");
        require(pmxer::ai::parsePlan(*plan).operations.size() == 1, "Unexpected plan");
        client.generate("invalid response", "{}");
        wait(client, "reject malformed plan");
        require(!client.state().error.empty() && !client.takePlan(), "Malformed output accepted");
        client.generate("failed turn", "{}");
        wait(client, "reject failed turn");
        require(!client.state().error.empty() && !client.takePlan(), "Failed turn accepted");
        client.generate("request tool", "{}");
        wait(client, "reject tool request");
        require(!client.state().error.empty() && !client.takePlan(), "Tool request accepted");
        client.generate("wait forever", "{}");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client.cancel();
        wait(client, "cancel turn");
        require(!client.takePlan(), "Canceled turn returned a plan");
        environment("PMXER_TEST_CODEX_ACCOUNT", "apiKey");
        client.generate("black color", "{}");
        wait(client, "reject API key account");
        require(!client.state().connected && !client.state().error.empty() && !client.takePlan(),
                "API key account bypassed ChatGPT sign-in");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
