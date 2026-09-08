#include "../../src/ui/UiAutomation.hpp"

#include <cassert>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

int main() {
#if defined(_WIN32)
    return 0;
#else
    pmxer::UiAutomationRegistry registry;
    registry.beginFrame();
    registry.registerWindow("outliner", "Outliner", 0, 0, 320, 720);
    for (std::size_t index = 0; index < 15000U; ++index) {
        pmxer::AutomationItem item;
        item.window = "outliner";
        item.id = "outliner/item:" + std::to_string(index);
        item.role = "tree_item";
        item.label = "Item " + std::to_string(index);
        item.visible = false;
        registry.registerItem(std::move(item));
    }

    const auto path = std::filesystem::temp_directory_path() /
                      ("pmxer-ui-test-" + std::to_string(getpid()) + ".sock");
    pmxer::UiAutomationServer server;
    assert(server.start(path));

    const auto connectClient = [&]() {
        const auto descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(descriptor >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto name = path.string();
        assert(name.size() < sizeof(address.sun_path));
        std::copy(name.begin(), name.end(), address.sun_path);
        assert(connect(descriptor, reinterpret_cast<const sockaddr *>(&address),
                       sizeof(address)) == 0);
        const auto flags = fcntl(descriptor, F_GETFL, 0);
        assert(flags >= 0);
        assert(fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0);
        return descriptor;
    };

    const auto readResponse = [&](int descriptor, std::string request) {
        const auto *data = request.data();
        auto remaining = request.size();
        while (remaining != 0U) {
            int flags = 0;
#if defined(MSG_NOSIGNAL)
            flags |= MSG_NOSIGNAL;
#endif
            const auto count = send(descriptor, data, remaining, flags);
            if (count > 0) {
                data += count;
                remaining -= static_cast<std::size_t>(count);
                continue;
            }
            if (errno == EPIPE || errno == ECONNRESET)
                break;
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            server.processPending(
                [&](std::string_view line) { return registry.handle(line); });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::string response;
        char buffer[16384];
        for (int attempt = 0; attempt < 10000; ++attempt) {
            server.processPending(
                [&](std::string_view line) { return registry.handle(line); });
            for (;;) {
                const auto count = recv(descriptor, buffer, sizeof(buffer), 0);
                if (count > 0) {
                    response.append(buffer, static_cast<std::size_t>(count));
                    if (response.find('\n') != std::string::npos)
                        return response;
                    continue;
                }
                if (count == 0)
                    return response;
                assert(errno == EAGAIN || errno == EWOULDBLOCK);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return response;
    };

    const auto treeClient = connectClient();
    const auto treeResponse = readResponse(treeClient, "{\"cmd\":\"tree\"}\n");
    close(treeClient);
    assert(treeResponse.find("outliner/item:14999") != std::string::npos);
    assert(!treeResponse.empty() && treeResponse.back() == '\n');

    const auto oversizedClient = connectClient();
    std::string oversized(1024U * 1024U + 1U, 'x');
    oversized.push_back('\n');
    const auto oversizedResponse = readResponse(oversizedClient, std::move(oversized));
    close(oversizedClient);
    assert(oversizedResponse.find("request_too_large") != std::string::npos);
    server.stop();
    return 0;
#endif
}
