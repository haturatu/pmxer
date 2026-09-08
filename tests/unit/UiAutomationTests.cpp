#include "../../src/ui/UiAutomation.hpp"

#include <cassert>
#include <string>
#include <utility>

int main() {
    pmxer::UiAutomationRegistry registry;
    registry.beginFrame();
    registry.registerWindow("inspector", "Inspector", 1, 2, 3, 4);
    int clicks{};
    std::string value{"before"};
    pmxer::AutomationItem item;
    item.window = "inspector";
    item.id = "inspector/name";
    item.role = "text_input";
    item.label = "Name";
    item.value = value;
    item.click = [&clicks]() { ++clicks; };
    item.set = [&value](std::string_view next) {
        value = next;
        return pmxer::AutomationResult{};
    };
    registry.registerItem(std::move(item));

    const auto tree = registry.handle(R"({"cmd":"tree"})");
    assert(tree.find("\"ok\":true") != std::string::npos);
    assert(tree.find("\"rect\":[1,2,3,4]") != std::string::npos);
    assert(tree.find("\"id\":\"inspector/name\"") != std::string::npos);

    assert(registry.handle(R"({"cmd":"click","target":"inspector/name"})") ==
           "{\"ok\":true}");
    assert(clicks == 1);
    assert(registry.handle(
               R"({"cmd":"set","target":"inspector/name","value":"after"})") ==
           "{\"ok\":true}");
    assert(value == "after");
    std::string key;
    registry.setKeyHandler([&key](std::string_view next) {
        key = next;
        return std::string{"{\"ok\":true}"};
    });
    assert(registry.handle(R"({"cmd":"key","target":"ctrl+z"})") ==
           "{\"ok\":true}");
    assert(key == "ctrl+z");
    assert(registry.handle(R"({"cmd":"wait","target":"inspector/name"})") ==
           "{\"ok\":true,\"ready\":true}");
    assert(registry.handle(R"({"cmd":"click","target":"missing"})")
               .find("widget_not_found") != std::string::npos);
    pmxer::AutomationItem invalid;
    invalid.window = "inspector";
    invalid.id = "inspector/invalid";
    invalid.role = "text_input";
    invalid.set = [](std::string_view) {
        return pmxer::AutomationResult{false, "invalid_value"};
    };
    registry.registerItem(std::move(invalid));
    assert(registry.handle(
               R"({"cmd":"set","target":"inspector/invalid","value":"x"})") ==
           "{\"ok\":false,\"error\":\"invalid_value\"}");
    std::string inputOperation;
    std::string inputTarget;
    std::string inputValue;
    registry.setInputHandler(
        [&inputOperation, &inputTarget, &inputValue](std::string_view operation,
                                                      std::string_view target,
                                                      std::string_view next) {
            inputOperation = operation;
            inputTarget = target;
            inputValue = next;
            return pmxer::AutomationResult{};
        });
    assert(registry.handle(
               R"({"cmd":"mouse","target":"10","value":"20"})") ==
           "{\"ok\":true}");
    assert(inputOperation == "mouse");
    assert(inputTarget == "10");
    assert(inputValue == "20");
    return 0;
}
