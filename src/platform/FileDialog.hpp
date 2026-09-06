#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace pmxer {

struct FileDialogResult {
    std::filesystem::path path;
    bool save{};
};

class FileDialog {
  public:
    explicit FileDialog(SDL_Window *window) noexcept : window_(window) {}

    bool open(const std::filesystem::path &directory = {});
    bool save(const std::filesystem::path &suggested = {});
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] std::optional<FileDialogResult> takeResult();
    [[nodiscard]] std::optional<std::string> takeError();

  private:
    static void SDLCALL callback(void *userdata, const char *const *files, int filter);
    bool begin(const std::filesystem::path &location, bool saveMode);

    SDL_Window *window_{};
    mutable std::mutex mutex_;
    std::string location_;
    bool busy_{};
    bool saveMode_{};
    std::optional<FileDialogResult> result_;
    std::optional<std::string> error_;
};

} // namespace pmxer
