#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace pmxer::ai {
struct CodexOptions {
    std::chrono::milliseconds rpcTimeout{std::chrono::seconds(90)};
    std::chrono::milliseconds idleTimeout{std::chrono::minutes(5)};
    std::chrono::milliseconds planTimeout{std::chrono::minutes(10)};
    std::chrono::milliseconds loginTimeout{std::chrono::minutes(10)};
    std::string effort{"medium"}; // Empty preserves the Codex reasoning setting.
};
// One short-lived app-server per job. OAuth and credential persistence belong to Codex.
class CodexClient {
  public:
    enum class Phase { connecting, checkingAccount, signingIn, preparing, planning, receiving };
    struct State {
        bool busy{};
        bool connected{};
        std::string account;
        std::string error;
        std::optional<std::string> plan;
        std::string authUrl; // Never logged or persisted by pmxer.
        Phase phase{Phase::connecting};
        bool reconnecting{};
        bool unavailable{};
        int elapsedSeconds{}, inactiveSeconds{};
    };
    ~CodexClient();
    void connect(bool allowLogin, CodexOptions options = {});
    void generate(std::string prompt, std::string context, std::string model = {},
                  CodexOptions options = {});
    void cancel() noexcept;
    [[nodiscard]] State state() const;
    [[nodiscard]] std::optional<std::string> takePlan();

  private:
    void start(bool login, std::string prompt, std::string context, std::string model,
               CodexOptions options);
    mutable std::mutex mutex_;
    State state_;
    std::atomic_bool canceled_{};
    std::thread worker_;
    std::chrono::steady_clock::time_point startedAt_, lastActivity_;
};
} // namespace pmxer::ai
