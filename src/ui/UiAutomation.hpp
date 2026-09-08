#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pmxer {

struct AutomationItem {
    std::string window;
    std::string id;
    std::string role;
    std::string label;
    std::string value;
    std::string tooltip;
    bool visible{true};
    bool enabled{true};
    bool focused{};
    bool hovered{};
    bool selected{};
    bool expanded{};
    int x{};
    int y{};
    int width{};
    int height{};
    std::function<void()> click;
    std::function<void(std::string_view)> set;
};

class UiAutomationRegistry {
  public:
    void beginFrame();
    void registerWindow(std::string id, std::string label, int x, int y,
                        int width, int height);
    void registerItem(AutomationItem item);
    void setStateJson(std::string state);
    void setKeyHandler(std::function<std::string(std::string_view)> handler);

    [[nodiscard]] std::string handle(std::string_view request);

  private:
    struct Window {
        std::string id;
        std::string label;
        int x{};
        int y{};
        int width{};
        int height{};
    };

    std::vector<Window> windows_;
    std::vector<AutomationItem> items_;
    std::string stateJson_;
    std::function<std::string(std::string_view)> keyHandler_;
};

class UiAutomationServer {
  public:
    UiAutomationServer() = default;
    UiAutomationServer(const UiAutomationServer &) = delete;
    UiAutomationServer &operator=(const UiAutomationServer &) = delete;
    ~UiAutomationServer();

    [[nodiscard]] bool start(const std::filesystem::path &path);
    void stop();
    void processPending(
        const std::function<std::string(std::string_view)> &handler);
    [[nodiscard]] bool running() const noexcept;

  private:
    struct Connection {
        int descriptor{-1};
        std::string input;
    };

    int listener_{-1};
    std::filesystem::path path_;
    std::vector<Connection> connections_;
};

} // namespace pmxer
