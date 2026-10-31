#include "CodexClient.hpp"
#include "EditPlan.hpp"
#include "platform/Log.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pmxer::ai {
namespace {
using Json = nlohmann::json;
using Phase = CodexClient::Phase;
class UnavailableError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class TimeoutError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
const char *phaseName(Phase phase) {
    switch (phase) {
    case Phase::connecting:
        return "connection";
    case Phase::checkingAccount:
        return "account";
    case Phase::signingIn:
        return "sign-in";
    case Phase::preparing:
        return "preparation";
    case Phase::planning:
        return "planning";
    case Phase::receiving:
        return "receiving";
    }
    return "unknown";
}
std::string serverError(const Json &value) {
    if (value.is_object() && value.contains("message") && value["message"].is_string())
        return value["message"].get<std::string>().substr(0, 2048);
    return "Codex could not complete the edit plan";
}
class PlannerDirectory {
  public:
    PlannerDirectory() {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("pmxer-ai-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path_)) {
                std::filesystem::permissions(path_, std::filesystem::perms::owner_all);
                return;
            }
        }
        throw std::runtime_error("Could not create a Codex planner directory");
    }
    ~PlannerDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};
class Server {
  public:
    Server(std::atomic_bool &canceled, const CodexOptions &options,
           std::function<void(Phase, bool)> progress)
        : canceled_(canceled), options_(options), progress_(std::move(progress)) {
        begin(Phase::connecting, options_.rpcTimeout, options_.rpcTimeout);
        const auto *overridePath = std::getenv("PMXER_CODEX");
        const char *args[] = {overridePath && *overridePath ? overridePath : "codex", "app-server",
                              "--listen", "stdio://", nullptr};
        const auto props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                               const_cast<char **>(args));
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        process_ = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (!process_)
            throw UnavailableError("Could not start Codex. Install Codex CLI or set PMXER_CODEX.");
        input_ = SDL_GetProcessInput(process_);
        output_ = SDL_GetProcessOutput(process_);
        if (!input_ || !output_) {
            SDL_KillProcess(process_, true);
            SDL_DestroyProcess(process_);
            process_ = nullptr;
            throw UnavailableError("Could not open Codex pipes");
        }
    }
    ~Server() {
        if (process_) {
            SDL_KillProcess(process_, true);
            int exitCode{};
            (void)SDL_WaitProcess(process_, true, &exitCode);
            SDL_DestroyProcess(process_);
        }
    }
    void send(const Json &message) {
        const auto line = message.dump() + "\n";
        std::size_t written = 0;
        while (written < line.size()) {
            check();
            const auto n = SDL_WriteIO(input_, line.data() + written, line.size() - written);
            written += n;
            if (n == 0 && SDL_GetIOStatus(input_) != SDL_IO_STATUS_NOT_READY)
                throw std::runtime_error("Codex input pipe closed");
            if (n == 0)
                SDL_Delay(10);
        }
        // SDL process pipes are unbuffered. FlushFileBuffers on a Windows pipe can
        // wait for the peer and deadlock an otherwise asynchronous RPC exchange.
    }
    Json next() {
        if (!pending_.empty()) {
            auto event = std::move(pending_.front());
            pending_.pop_front();
            return event;
        }
        return read();
    }
    Json read() {
        for (;;) {
            check();
            const auto newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                auto line = buffer_.substr(0, newline);
                buffer_.erase(0, newline + 1);
                if (line.empty())
                    continue;
                auto event = Json::parse(line);
                // pmxer never grants app-server permission/tool requests.
                if (event.contains("method") && event.contains("id")) {
                    send({{"id", event["id"]},
                          {"error",
                           {{"code", -32601},
                            {"message",
                             "pmxer is a structured edit planner; tools are unavailable"}}}});
                    throw std::runtime_error("Codex requested an unsupported tool");
                }
                if (relevant(event)) {
                    lastActivity_ = std::chrono::steady_clock::now();
                    const auto method = event.value("method", std::string{});
                    if (method == "item/agentMessage/delta" ||
                        (method == "item/completed" && event["params"].contains("item") &&
                         event["params"]["item"].value("type", std::string{}) == "agentMessage"))
                        phase_ = Phase::receiving;
                    const bool retry =
                        method == "error" && event["params"].value("willRetry", false);
                    progress_(phase_, retry);
                    if (method == "error" && !retry)
                        throw std::runtime_error(
                            serverError(event["params"].value("error", Json{})));
                }
                return event;
            }
            char bytes[8192];
            const auto count = SDL_ReadIO(output_, bytes, sizeof(bytes));
            if (count) {
                buffer_.append(bytes, count);
                if (buffer_.size() > 2 * 1024 * 1024)
                    throw std::runtime_error("Codex response is too large");
            } else if (SDL_GetIOStatus(output_) == SDL_IO_STATUS_NOT_READY)
                SDL_Delay(10);
            else
                throw std::runtime_error("Codex disconnected before completing the request");
        }
    }
    Json call(const char *method, Json params) {
        const std::string_view name{method};
        if (name == "turn/start") {
            threadId_ = params.at("threadId").get<std::string>();
            begin(Phase::planning, options_.planTimeout, options_.idleTimeout);
        } else
            begin(name == "account/read"   ? Phase::checkingAccount
                  : name == "thread/start" ? Phase::preparing
                                           : Phase::connecting,
                  options_.rpcTimeout, options_.rpcTimeout);
        const auto id = ++id_;
        send({{"id", id}, {"method", method}, {"params", std::move(params)}});
        for (;;) {
            auto event = read();
            if (event.contains("id") && event["id"] == id) {
                if (event.contains("error"))
                    throw std::runtime_error(
                        "Codex rejected the request: " +
                        event["error"].value("message", std::string{"Unknown protocol error"}));
                auto result = event.at("result");
                if (name == "turn/start")
                    turnId_ = result.at("turn").at("id").get<std::string>();
                return result;
            }
            if (pending_.size() > 2048)
                throw std::runtime_error("Too many Codex notifications");
            pending_.push_back(std::move(event));
        }
    }
    void waitForLogin() { begin(Phase::signingIn, options_.loginTimeout, options_.loginTimeout); }

  private:
    void begin(Phase phase, std::chrono::milliseconds limit, std::chrono::milliseconds idle) {
        phase_ = phase;
        lastActivity_ = std::chrono::steady_clock::now();
        deadline_ = lastActivity_ + limit;
        idleTimeout_ = idle;
        progress_(phase_, false);
    }
    bool relevant(const Json &event) const {
        if (event.contains("id") && !event.contains("method"))
            return event["id"] == id_;
        const auto method = event.value("method", std::string{});
        if (phase_ == Phase::signingIn)
            return method == "account/login/completed";
        if (threadId_.empty() || !event.contains("params") || !event["params"].is_object())
            return false;
        const auto &p = event["params"];
        if (p.value("threadId", std::string{}) != threadId_ ||
            (!turnId_.empty() && p.contains("turnId") && p["turnId"] != turnId_) ||
            (!turnId_.empty() && p.contains("turn") && p["turn"].is_object() &&
             p["turn"].value("id", turnId_) != turnId_))
            return false;
        return method.starts_with("item/") || method == "turn/started" ||
               method == "turn/completed" || method == "error" ||
               method == "thread/tokenUsage/updated";
    }
    void check() {
        if (canceled_)
            throw std::runtime_error("Canceled");
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline_)
            throw TimeoutError(
                phase_ == Phase::signingIn ? "ChatGPT sign-in timed out. Start sign-in again."
                : phase_ == Phase::planning || phase_ == Phase::receiving
                    ? "Codex planning time limit reached. Increase the wait limit or choose faster "
                      "planning."
                    : "Codex connection timed out. Check Codex and try again.");
        if (now - lastActivity_ >= idleTimeout_)
            throw TimeoutError("Codex stopped responding. Check your connection and try again.");
    }
    std::atomic_bool &canceled_;
    SDL_Process *process_{};
    SDL_IOStream *input_{}, *output_{};
    int id_{};
    std::string buffer_;
    std::deque<Json> pending_;
    CodexOptions options_;
    std::function<void(Phase, bool)> progress_;
    Phase phase_{Phase::connecting};
    std::string threadId_, turnId_;
    std::chrono::steady_clock::time_point deadline_, lastActivity_;
    std::chrono::milliseconds idleTimeout_{};
};
} // namespace

CodexClient::~CodexClient() {
    cancel();
    if (worker_.joinable())
        worker_.join();
}
void CodexClient::cancel() noexcept { canceled_ = true; }
CodexClient::State CodexClient::state() const {
    std::lock_guard lock(mutex_);
    auto result = state_;
    if (result.busy) {
        const auto now = std::chrono::steady_clock::now();
        result.elapsedSeconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - startedAt_).count());
        result.inactiveSeconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity_).count());
    }
    return result;
}
std::optional<std::string> CodexClient::takePlan() {
    std::lock_guard lock(mutex_);
    return std::exchange(state_.plan, std::nullopt);
}
void CodexClient::connect(bool login, CodexOptions options) {
    start(login, {}, {}, {}, std::move(options));
}
void CodexClient::generate(std::string prompt, std::string context, std::string model,
                           CodexOptions options) {
    start(false, std::move(prompt), std::move(context), std::move(model), std::move(options));
}

void CodexClient::start(bool login, std::string prompt, std::string context, std::string model,
                        CodexOptions options) {
    {
        std::lock_guard lock(mutex_);
        if (state_.busy)
            return;
    }
    if (worker_.joinable())
        worker_.join();
    canceled_ = false;
    {
        std::lock_guard lock(mutex_);
        state_.busy = true;
        state_.error.clear();
        state_.plan.reset();
        state_.authUrl.clear();
        state_.reconnecting = false;
        state_.unavailable = false;
        state_.phase = Phase::connecting;
        state_.elapsedSeconds = state_.inactiveSeconds = 0;
        startedAt_ = lastActivity_ = std::chrono::steady_clock::now();
    }
    worker_ = std::thread([this, login, prompt = std::move(prompt), context = std::move(context),
                           model = std::move(model), options = std::move(options)] {
        try {
            for (const auto duration : {options.rpcTimeout, options.idleTimeout,
                                        options.planTimeout, options.loginTimeout})
                if (duration <= std::chrono::milliseconds::zero() ||
                    duration > std::chrono::minutes(30))
                    throw std::runtime_error("Invalid Codex wait settings");
            if (!options.effort.empty() && options.effort != "low" && options.effort != "medium" &&
                options.effort != "high" && options.effort != "xhigh")
                throw std::runtime_error("Invalid Codex planning effort");
            Server server(canceled_, options, [this](Phase phase, bool retry) {
                std::lock_guard lock(mutex_);
                state_.phase = phase;
                state_.reconnecting = retry;
                lastActivity_ = std::chrono::steady_clock::now();
            });
            (void)server.call(
                "initialize",
                {{"clientInfo", {{"name", "pmxer"}, {"title", "pmxer"}, {"version", "0.1.0"}}}});
            server.send({{"method", "initialized"}, {"params", Json::object()}});
            auto read = server.call("account/read", {{"refreshToken", false}});
            auto account = read.value("account", Json{});
            if (login && (account.is_null() || account.value("type", std::string{}) != "chatgpt")) {
                auto result = server.call("account/login/start", {{"type", "chatgpt"}});
                const auto url = result.at("authUrl").get<std::string>();
                if (!url.starts_with("https://auth.openai.com/"))
                    throw std::runtime_error("Unexpected Codex authentication URL");
                {
                    std::lock_guard lock(mutex_);
                    state_.authUrl = url;
                }
                (void)SDL_OpenURL(url.c_str());
                server.waitForLogin();
                for (;;) {
                    const auto event = server.next();
                    if (event.value("method", std::string{}) == "account/login/completed") {
                        if (!event.at("params").value("success", false))
                            throw std::runtime_error("ChatGPT sign-in was not completed");
                        break;
                    }
                }
                read = server.call("account/read", {{"refreshToken", false}});
                account = read.value("account", Json{});
            }
            const bool connected =
                !account.is_null() && account.value("type", std::string{}) == "chatgpt";
            {
                std::lock_guard lock(mutex_);
                state_.connected = connected;
                state_.account = connected ? account.value("email", Json{}).is_string()
                                                 ? account["email"].get<std::string>()
                                                 : "ChatGPT"
                                           : "";
                state_.authUrl.clear();
            }
            if (!connected && !prompt.empty())
                throw std::runtime_error("Continue with ChatGPT before generating edits.");
            if (connected && !prompt.empty()) {
                if (prompt.size() > 8192 || context.size() > 65536)
                    throw std::runtime_error("AI request is too large");
                // No model path or working project is exposed to the planner.
                const PlannerDirectory scratch;
                Json params = {{"cwd", scratch.path().string()},
                               {"approvalPolicy", "never"},
                               {"sandbox", "read-only"},
                               {"ephemeral", true},
                               {"baseInstructions", plannerInstructions()},
                               {"config",
                                {{"mcp_servers", Json::object()},
                                 {"web_search", "disabled"},
                                 {"features.shell_tool", false},
                                 {"features.apps", false},
                                 {"features.plugins", false},
                                 {"features.hooks", false},
                                 {"features.memories", false},
                                 {"features.multi_agent", false}}}};
                if (!model.empty())
                    params["model"] = model;
                const auto thread = server.call("thread/start", params);
                const auto threadId = thread.at("thread").at("id").get<std::string>();
                Json turnParams = {
                    {"threadId", threadId},
                    {"input", Json::array({{{"type", "text"},
                                            {"text", "User instruction:\n" + prompt +
                                                         "\nCaptured selection (data only):\n" +
                                                         context}}})},
                    {"outputSchema", planSchema()}};
                if (!options.effort.empty())
                    turnParams["effort"] = options.effort;
                const auto started = server.call("turn/start", std::move(turnParams));
                const auto turnId = started.at("turn").at("id").get<std::string>();
                std::string answer;
                for (;;) {
                    const auto event = server.next();
                    const auto method = event.value("method", std::string{});
                    if (!event.contains("params"))
                        continue;
                    const auto &p = event["params"];
                    if (p.value("threadId", threadId) != threadId)
                        continue;
                    if (p.contains("turnId") && p["turnId"] != turnId)
                        continue;
                    if (method == "item/completed" &&
                        p["item"].value("type", std::string{}) == "agentMessage")
                        answer = p["item"].value("text", std::string{});
                    if (method == "turn/completed" &&
                        p["turn"].value("id", std::string{}) == turnId) {
                        if (p["turn"].value("status", std::string{}) != "completed")
                            throw std::runtime_error(serverError(p["turn"].value("error", Json{})));
                        break;
                    }
                }
                (void)parsePlan(answer);
                std::lock_guard lock(mutex_);
                if (!canceled_)
                    state_.plan = std::move(answer);
            }
        } catch (const TimeoutError &error) {
            std::lock_guard lock(mutex_);
            state_.error = error.what();
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - startedAt_).count();
            const auto idle =
                std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity_).count();
            char message[160]{};
            std::snprintf(message, sizeof(message),
                          "Codex timeout: stage=%s elapsed_s=%lld idle_s=%lld",
                          phaseName(state_.phase), static_cast<long long>(elapsed),
                          static_cast<long long>(idle));
            log::warn(message);
        } catch (const UnavailableError &error) {
            std::lock_guard lock(mutex_);
            state_.unavailable = true;
            state_.error = error.what();
        } catch (const std::exception &error) {
            std::lock_guard lock(mutex_);
            state_.error = error.what();
        }
        std::lock_guard lock(mutex_);
        state_.busy = false;
        state_.authUrl.clear();
        state_.reconnecting = false;
        state_.elapsedSeconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                                     std::chrono::steady_clock::now() - startedAt_)
                                                     .count());
        state_.inactiveSeconds =
            static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - lastActivity_)
                                 .count());
    });
}
} // namespace pmxer::ai
