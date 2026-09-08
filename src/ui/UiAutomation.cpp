#include "UiAutomation.hpp"

#include "../automation/AutomationProtocol.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace pmxer {
namespace {

[[maybe_unused]] constexpr std::size_t maximumRequestSize = 1024U * 1024U;

std::string jsonString(std::string_view request, std::string_view key) {
    const auto quotedKey = automation::escapeJson(key);
    const auto keyPosition = request.find(quotedKey);
    if (keyPosition == std::string_view::npos)
        return {};
    auto position = request.find(':', keyPosition + quotedKey.size());
    if (position == std::string_view::npos)
        return {};
    ++position;
    while (position < request.size() &&
           (request[position] == ' ' || request[position] == '\t'))
        ++position;
    if (position >= request.size() || request[position] != '"')
        return {};
    ++position;
    std::string result;
    bool escaped = false;
    for (; position < request.size(); ++position) {
        const auto character = request[position];
        if (escaped) {
            switch (character) {
            case '"':
            case '\\':
            case '/':
                result.push_back(character);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(character);
                break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return result;
        } else {
            result.push_back(character);
        }
    }
    return {};
}

std::string boolJson(bool value) { return value ? "true" : "false"; }

std::string itemJson(const AutomationItem &item) {
    std::string result = "{\"id\":" + automation::escapeJson(item.id) +
                         ",\"role\":" + automation::escapeJson(item.role) +
                         ",\"label\":" + automation::escapeJson(item.label) +
                         ",\"value\":" + automation::escapeJson(item.value) +
                         ",\"tooltip\":" + automation::escapeJson(item.tooltip) +
                         ",\"rect\":[" + std::to_string(item.x) + "," +
                         std::to_string(item.y) + "," +
                         std::to_string(item.width) + "," +
                         std::to_string(item.height) + "]";
    result += ",\"visible\":" + boolJson(item.visible);
    result += ",\"enabled\":" + boolJson(item.enabled);
    result += ",\"focused\":" + boolJson(item.focused);
    result += ",\"hovered\":" + boolJson(item.hovered);
    result += ",\"selected\":" + boolJson(item.selected);
    result += ",\"expanded\":" + boolJson(item.expanded) + "}";
    return result;
}

std::string errorJson(std::string_view error) {
    return "{\"ok\":false,\"error\":" + automation::escapeJson(error) +
           "}";
}

#if !defined(_WIN32)
bool makeNonBlocking(int descriptor) {
    const auto flags = fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeDescriptor(int &descriptor) {
    if (descriptor >= 0) {
        close(descriptor);
        descriptor = -1;
    }
}
#endif

} // namespace

void UiAutomationRegistry::beginFrame() {
    windows_.clear();
    items_.clear();
    itemIndices_.clear();
}

void UiAutomationRegistry::registerWindow(std::string id, std::string label,
                                          int x, int y, int width, int height) {
    windows_.push_back(
        Window{std::move(id), std::move(label), x, y, width, height});
}

void UiAutomationRegistry::registerItem(AutomationItem item) {
    if (item.id.empty())
        return;
    const auto [iterator, inserted] = itemIndices_.emplace(item.id, items_.size());
    if (inserted)
        items_.push_back(std::move(item));
    else
        items_[iterator->second] = std::move(item);
}

void UiAutomationRegistry::setStateJson(std::string state) {
    stateJson_ = std::move(state);
}

void UiAutomationRegistry::setKeyHandler(
    std::function<std::string(std::string_view)> handler) {
    keyHandler_ = std::move(handler);
}

void UiAutomationRegistry::setInputHandler(
    std::function<AutomationResult(std::string_view, std::string_view,
                                    std::string_view)> handler) {
    inputHandler_ = std::move(handler);
}

std::string UiAutomationRegistry::handle(std::string_view request) {
    const auto command = jsonString(request, "cmd");
    if (command == "tree") {
        std::string result = "{\"ok\":true,\"windows\":[";
        for (std::size_t index = 0; index < windows_.size(); ++index) {
            if (index != 0U)
                result.push_back(',');
            const auto &window = windows_[index];
            result += "{\"id\":" + automation::escapeJson(window.id) +
                      ",\"label\":" + automation::escapeJson(window.label) +
                      ",\"rect\":[" + std::to_string(window.x) + "," +
                      std::to_string(window.y) + "," +
                      std::to_string(window.width) + "," +
                      std::to_string(window.height) + "]";
            result += ",\"items\":[";
            bool firstItem = true;
            for (const auto &item : items_) {
                if (item.window != window.id)
                    continue;
                if (!firstItem)
                    result.push_back(',');
                firstItem = false;
                result += itemJson(item);
            }
            result += "]}";
        }
        result += "]}";
        return result;
    }
    if (command == "state" || command == "sessions") {
        if (stateJson_.empty())
            return "{\"ok\":true,\"state\":{}}";
        return "{\"ok\":true,\"state\":" + stateJson_ + "}";
    }
    if (command == "frame")
        return "{\"ok\":true}";
    if (command == "key") {
        if (!keyHandler_)
            return errorJson("key_input_unavailable");
        const auto key = jsonString(request, "target");
        const auto response = keyHandler_(key);
        return response.empty() ? "{\"ok\":true}" : response;
    }
    if (command == "mouse" || command == "mouse-move" ||
        command == "mouse-down" || command == "mouse-up" ||
        command == "key-down" || command == "key-up" ||
        command == "text" || command == "text-input" || command == "wheel") {
        if (!inputHandler_)
            return errorJson("real_input_unavailable");
        const auto result = inputHandler_(
            command, jsonString(request, "target"),
            jsonString(request, "value"));
        return result.success
                   ? "{\"ok\":true}"
                   : errorJson(result.error.empty() ? "input_failed"
                                                     : result.error);
    }

    const auto target = jsonString(request, "target");
    const auto found = std::find_if(
        items_.begin(), items_.end(),
        [&](const auto &item) { return item.id == target; });
    if (command == "wait") {
        const auto ready = found != items_.end() && found->visible;
        return "{\"ok\":true,\"ready\":" + boolJson(ready) + "}";
    }
    if (found == items_.end()) {
        std::string result = errorJson("widget_not_found");
        result.pop_back();
        result += ",\"candidates\":[";
        std::size_t count{};
        for (const auto &item : items_) {
            if (count != 0U)
                result.push_back(',');
            result += automation::escapeJson(item.id);
            ++count;
            if (count == 20U)
                break;
        }
        result += "]}";
        return result;
    }
    if (!found->enabled)
        return errorJson("widget_unavailable");
    if (command == "click") {
        if (!found->click)
            return errorJson("widget_not_clickable");
        found->click();
        return found->visible ? "{\"ok\":true}"
                              : "{\"ok\":true,\"pending\":true}";
    }
    if (!found->visible)
        return errorJson("widget_unavailable");
    if (command == "set") {
        if (!found->set)
            return errorJson("widget_not_editable");
        const auto result = found->set(jsonString(request, "value"));
        return result.success
                   ? "{\"ok\":true}"
                   : errorJson(result.error.empty() ? "edit_failed"
                                                     : result.error);
    }
    return errorJson("unknown_command");
}

UiAutomationServer::~UiAutomationServer() { stop(); }

bool UiAutomationServer::start(const std::filesystem::path &path) {
#if defined(_WIN32)
    (void)path;
    return false;
#else
    stop();
    if (path.empty())
        return false;
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    const auto name = path.string();
    listener_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener_ < 0)
        return false;
    sockaddr_un address{};
    if (name.size() >= sizeof(address.sun_path)) {
        closeDescriptor(listener_);
        return false;
    }
    address.sun_family = AF_UNIX;
    std::copy(name.begin(), name.end(), address.sun_path);
    unlink(name.c_str());
    if (bind(listener_, reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) != 0 ||
        listen(listener_, 8) != 0 || !makeNonBlocking(listener_)) {
        stop();
        return false;
    }
    path_ = path;
    return true;
#endif
}

void UiAutomationServer::stop() {
#if defined(_WIN32)
    listener_ = -1;
    path_.clear();
    connections_.clear();
#else
    for (auto &connection : connections_)
        closeDescriptor(connection.descriptor);
    connections_.clear();
    closeDescriptor(listener_);
    if (!path_.empty()) {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
    path_.clear();
#endif
}

void UiAutomationServer::processPending(
    const std::function<std::string(std::string_view)> &handler) {
#if defined(_WIN32)
    (void)handler;
#else
    if (listener_ < 0)
        return;
    for (;;) {
        const auto descriptor = accept(listener_, nullptr, nullptr);
        if (descriptor < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            break;
        }
        if (!makeNonBlocking(descriptor)) {
            close(descriptor);
            continue;
        }
#if defined(SO_NOSIGPIPE)
        int noSignal = 1;
        (void)setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &noSignal,
                         sizeof(noSignal));
#endif
        Connection connection;
        connection.descriptor = descriptor;
        connections_.push_back(std::move(connection));
    }

    for (auto iterator = connections_.begin(); iterator != connections_.end();) {
        if (!iterator->responseReady) {
            bool inputFinished = false;
            char buffer[4096];
            for (;;) {
                const auto count = recv(iterator->descriptor, buffer,
                                        sizeof(buffer), 0);
                if (count > 0) {
                    const auto bytes = static_cast<std::size_t>(count);
                    if (iterator->input.size() + bytes > maximumRequestSize) {
                        iterator->output = errorJson("request_too_large");
                        iterator->output.push_back('\n');
                        iterator->responseReady = true;
                        break;
                    }
                    iterator->input.append(buffer, bytes);
                    if (iterator->input.find('\n') != std::string::npos) {
                        inputFinished = true;
                        break;
                    }
                    continue;
                }
                if (count == 0) {
                    inputFinished = true;
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                inputFinished = true;
                break;
            }
            if (inputFinished && !iterator->responseReady) {
                const auto lineEnd = iterator->input.find('\n');
                if (lineEnd == std::string::npos) {
                    iterator->output = errorJson("incomplete_request");
                } else {
                    const auto request = iterator->input.substr(0, lineEnd);
                    try {
                        iterator->output = handler(request);
                    } catch (const std::exception &error) {
                        iterator->output = errorJson(error.what());
                    }
                }
                iterator->output.push_back('\n');
                iterator->responseReady = true;
            }
        }
        if (!iterator->responseReady) {
            ++iterator;
            continue;
        }
        bool sendFailed = false;
        while (iterator->outputOffset < iterator->output.size()) {
            const auto *data = iterator->output.data() +
                               static_cast<std::ptrdiff_t>(
                                   iterator->outputOffset);
            const auto remaining = iterator->output.size() -
                                    iterator->outputOffset;
            int flags = 0;
#if defined(MSG_NOSIGNAL)
            flags |= MSG_NOSIGNAL;
#endif
            const auto count = send(iterator->descriptor, data, remaining,
                                    flags);
            if (count > 0) {
                iterator->outputOffset += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            sendFailed = true;
            break;
        }
        if (sendFailed || iterator->outputOffset == iterator->output.size()) {
            closeDescriptor(iterator->descriptor);
            iterator = connections_.erase(iterator);
        } else {
            ++iterator;
        }
    }
#endif
}

bool UiAutomationServer::running() const noexcept { return listener_ >= 0; }

} // namespace pmxer
