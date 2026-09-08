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
    item.set = [&value](std::string_view next) { value = next; };
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
    assert(registry.handle(R"({"cmd":"wait","target":"inspector/name"})") ==
           "{\"ok\":true,\"ready\":true}");
    assert(registry.handle(R"({"cmd":"click","target":"missing"})")
               .find("widget_not_found") != std::string::npos);
    return 0;
}
