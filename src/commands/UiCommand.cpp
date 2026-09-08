#include "UiCommand.hpp"

#include "../automation/AutomationProtocol.hpp"
#include "../platform/Log.hpp"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace pmxer {
namespace {

std::string requestJson(const UiCommand &command) {
    std::string request = "{\"cmd\":" +
                          automation::escapeJson(command.operation);
    if (!command.target.empty())
        request += ",\"target\":" + automation::escapeJson(command.target);
    if (!command.value.empty())
        request += ",\"value\":" + automation::escapeJson(command.value);
    request += "}\n";
    return request;
}

bool successful(std::string_view response) {
    return response.find("\"ok\":true") != std::string_view::npos;
}

std::string shellQuote(std::string_view value) {
    std::string result{"'"};
    for (const auto character : value) {
        if (character == '\'')
            result += "'\\''";
        else
            result.push_back(character);
    }
    result.push_back('\'');
    return result;
}

#if !defined(_WIN32)
std::string connectAndRequest(const UiCommand &command) {
    const auto path = command.socket.empty() ? automation::defaultSocketPath()
                                             : command.socket;
    const auto name = path.string();
    const auto descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0)
        return "{\"ok\":false,\"error\":\"socket_create_failed\"}";
    sockaddr_un address{};
    if (name.size() >= sizeof(address.sun_path)) {
        close(descriptor);
        return "{\"ok\":false,\"error\":\"socket_path_too_long\"}";
    }
    address.sun_family = AF_UNIX;
    std::copy(name.begin(), name.end(), address.sun_path);
    if (connect(descriptor, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) != 0) {
        close(descriptor);
        return "{\"ok\":false,\"error\":\"no_running_editor\"}";
    }

    const auto request = requestJson(command);
    const char *data = request.data();
    std::size_t remaining = request.size();
    while (remaining != 0U) {
        const auto count = send(descriptor, data, remaining, 0);
        if (count <= 0) {
            close(descriptor);
            return "{\"ok\":false,\"error\":\"request_failed\"}";
        }
        data += count;
        remaining -= static_cast<std::size_t>(count);
    }

    const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration<float>(command.timeout));
    timeval socketTimeout{};
    socketTimeout.tv_sec = static_cast<decltype(socketTimeout.tv_sec)>(
        timeout.count() / 1000);
    socketTimeout.tv_usec = static_cast<decltype(socketTimeout.tv_usec)>(
        (timeout.count() % 1000) * 1000);
    (void)setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &socketTimeout,
                     sizeof(socketTimeout));

    std::string response;
    char buffer[4096];
    for (;;) {
        const auto count = recv(descriptor, buffer, sizeof(buffer), 0);
        if (count <= 0)
            break;
        response.append(buffer, static_cast<std::size_t>(count));
        if (response.find('\n') != std::string::npos)
            break;
    }
    close(descriptor);
    if (response.empty())
        return "{\"ok\":false,\"error\":\"response_timeout\"}";
    const auto lineEnd = response.find('\n');
    if (lineEnd != std::string::npos)
        response.resize(lineEnd);
    return response;
}
#endif

int captureScreenshot(const std::string &path) {
#if defined(_WIN32)
    (void)path;
    log::error("screen capture is not available on this platform build");
    return 3;
#elif defined(__APPLE__)
    const auto command = "screencapture -x " + shellQuote(path);
    return std::system(command.c_str()) == 0 ? 0 : 3;
#else
    const auto command = "spectacle -b -n -o " + shellQuote(path);
    return std::system(command.c_str()) == 0 ? 0 : 3;
#endif
}

} // namespace

int runUiCommand(const UiCommand &command) {
#if defined(_WIN32)
    (void)command;
    log::error("UI automation is not available on this platform build");
    return 3;
#else
    if (command.operation == "screenshot") {
        UiCommand frame = command;
        frame.operation = "frame";
        const auto ready = connectAndRequest(frame);
        if (!successful(ready)) {
            std::fputs((ready + "\n").c_str(), stdout);
            return 3;
        }
        const auto result = captureScreenshot(command.target);
        if (result != 0)
            return result;
        const auto response =
            std::string{"{\"ok\":true,\"path\":"} +
            automation::escapeJson(command.target) + "}";
        std::fputs((response + "\n").c_str(), stdout);
        return 0;
    }

    if (command.operation == "wait") {
        const auto started = std::chrono::steady_clock::now();
        std::string response;
        for (;;) {
            response = connectAndRequest(command);
            if (!successful(response)) {
                std::fputs((response + "\n").c_str(), stdout);
                return 1;
            }
            if (response.find("\"ready\":true") != std::string::npos)
                break;
            const auto elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - started);
            if (elapsed.count() >= command.timeout)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::fputs((response + "\n").c_str(), stdout);
        return response.find("\"ready\":true") != std::string::npos ? 0 : 1;
    }

    const auto response = connectAndRequest(command);
    std::fputs((response + "\n").c_str(), stdout);
    return successful(response) ? 0 : 1;
#endif
}

} // namespace pmxer
