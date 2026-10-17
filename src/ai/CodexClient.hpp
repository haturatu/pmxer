#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace pmxer::ai {
// One short-lived app-server per job. OAuth and credential persistence belong to Codex.
class CodexClient {
  public:
    struct State {
        bool busy{};
        bool connected{};
        std::string account;
        std::string error;
        std::optional<std::string> plan;
        std::string authUrl; // Never logged or persisted by pmxer.
    };
    ~CodexClient();
    void connect(bool allowLogin);
    void generate(std::string prompt, std::string context, std::string model = {});
    void cancel() noexcept;
    [[nodiscard]] State state() const;
    [[nodiscard]] std::optional<std::string> takePlan();

  private:
    void start(bool login, std::string prompt, std::string context, std::string model);
    mutable std::mutex mutex_;
    State state_;
    std::atomic_bool canceled_{};
    std::thread worker_;
};
} // namespace pmxer::ai
