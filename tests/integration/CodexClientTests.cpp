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
        const auto *hang = std::getenv("PMXER_TEST_CODEX_HANG_SETUP");
        const auto *slow = std::getenv("PMXER_TEST_CODEX_SLOW_SETUP");
        if (hang && *hang && method == "initialize")
            continue;
        if (slow && *slow &&
            (method == "initialize" || method == "account/read" || method == "thread/start"))
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
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
            require(p["config"]["mcp_servers"].empty() && p["config"]["features.apps"] == false &&
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
            if (text.find("inherit effort") != std::string::npos)
                require(!request["params"].contains("effort"), "Codex effort was overridden");
            else
                require(request["params"].value("effort", std::string{}) ==
                            (text.find("fast plan") != std::string::npos ? "low" : "medium"),
                        "Incorrect planning effort");
            if (text.find("wait forever") != std::string::npos)
                continue;
            if (text.find("server fault") != std::string::npos ||
                text.find("retry recovery") != std::string::npos) {
                const bool retry = text.find("retry recovery") != std::string::npos;
                std::cout << Json{{"method", "error"},
                                  {"params",
                                   {{"threadId", "thread-test"},
                                    {"turnId", "turn-test"},
                                    {"willRetry", retry},
                                    {"error", {{"message", "Test service unavailable"}}}}}}
                                 .dump()
                          << std::endl;
                if (!retry)
                    continue; // Terminal errors need not be followed by turn/completed.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (text.find("stream progress") != std::string::npos ||
                text.find("unrelated progress") != std::string::npos ||
                text.find("wrong turn progress") != std::string::npos) {
                for (int i = 0; i < 8; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(75));
                    std::cout
                        << Json{{"method", "item/agentMessage/delta"},
                                {"params",
                                 {{"threadId", text.find("unrelated progress") != std::string::npos
                                                   ? "other-thread"
                                                   : "thread-test"},
                                  {"turnId", text.find("wrong turn progress") != std::string::npos
                                                 ? "other-turn"
                                                 : "turn-test"},
                                  {"delta", "partial"}}}}
                               .dump()
                        << std::endl;
                }
            }
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
                                  {"error", {{"message", "Test turn failure detail"}}},
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
        require(client.state().error == "Test turn failure detail", "Turn failure detail was lost");
        client.generate("request tool", "{}");
        wait(client, "reject tool request");
        require(!client.state().error.empty() && !client.takePlan(), "Tool request accepted");
        client.generate("wait forever", "{}");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client.cancel();
        wait(client, "cancel turn");
        require(!client.takePlan(), "Canceled turn returned a plan");
        using namespace std::chrono_literals;
        pmxer::ai::CodexOptions quick;
        quick.rpcTimeout = 2s;
        quick.idleTimeout = 300ms;
        quick.planTimeout = 2s;
        client.generate("stream progress", "{}", {}, quick);
        const auto observing = std::chrono::steady_clock::now() + 1s;
        while (client.state().phase != pmxer::ai::CodexClient::Phase::receiving &&
               client.state().busy && std::chrono::steady_clock::now() < observing)
            std::this_thread::sleep_for(5ms);
        require(client.state().phase == pmxer::ai::CodexClient::Phase::receiving,
                "Streaming progress was not exposed");
        wait(client, "accept active stream beyond idle duration");
        require(client.state().error.empty() && client.takePlan(), "Active stream timed out");
        client.generate("wait forever", "{}", {}, quick);
        wait(client, "idle timeout");
        require(client.state().error.find("stopped responding") != std::string::npos &&
                    !client.takePlan(),
                "An unresponsive stream was not stopped");
        for (const auto *prompt : {"unrelated progress", "wrong turn progress"}) {
            client.generate(prompt, "{}", {}, quick);
            wait(client, "ignore unrelated stream activity");
            require(client.state().error.find("stopped responding") != std::string::npos &&
                        !client.takePlan(),
                    "Unrelated notifications extended the deadline");
        }
        auto capped = quick;
        capped.planTimeout = 350ms;
        client.generate("stream progress", "{}", {}, capped);
        wait(client, "absolute planning limit");
        require(client.state().error.find("planning time limit") != std::string::npos &&
                    !client.takePlan(),
                "Progress bypassed the absolute deadline");
        client.generate("server fault", "{}", {}, quick);
        wait(client, "report terminal server error immediately");
        require(client.state().error == "Test service unavailable" && !client.takePlan(),
                "Server error was hidden by a timeout");
        client.generate("retry recovery", "{}", {}, quick);
        const auto retryDeadline = std::chrono::steady_clock::now() + 1s;
        while (!client.state().reconnecting && client.state().busy &&
               std::chrono::steady_clock::now() < retryDeadline)
            std::this_thread::sleep_for(5ms);
        require(client.state().reconnecting && client.state().error.empty(),
                "Server retry progress was not exposed");
        wait(client, "server retry recovery");
        require(client.state().error.empty() && client.takePlan(),
                "Recoverable server error failed");
        auto fast = quick;
        fast.effort = "low";
        client.generate("fast plan", "{}", {}, fast);
        wait(client, "fast planning effort");
        require(client.state().error.empty() && client.takePlan(), "Fast effort was not sent");
        fast.effort.clear();
        client.generate("inherit effort", "{}", {}, fast);
        wait(client, "inherit Codex planning effort");
        require(client.state().error.empty() && client.takePlan(),
                "Codex effort was not preserved");
        environment("PMXER_TEST_CODEX_SLOW_SETUP", "1");
        client.generate("black color", "{}", {}, capped);
        wait(client, "give planning a fresh budget after slow setup");
        require(client.state().error.empty() && client.takePlan(), "Setup consumed planning time");
        environment("PMXER_TEST_CODEX_SLOW_SETUP", "");
        environment("PMXER_TEST_CODEX_HANG_SETUP", "1");
        auto stalled = quick;
        stalled.rpcTimeout = 300ms;
        client.connect(false, stalled);
        wait(client, "connection timeout");
        require(client.state().error.find("connection timed out") != std::string::npos,
                "Connection timeout was not identified");
        environment("PMXER_TEST_CODEX_HANG_SETUP", "");
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
