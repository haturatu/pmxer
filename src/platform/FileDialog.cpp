#include "FileDialog.hpp"

#include <SDL3/SDL_dialog.h>

#include <utility>

namespace pmxer {
namespace {

const SDL_DialogFileFilter filters[] = {{"PMX model", "pmx;PMX"}, {"All files", "*"}};

} // namespace

bool FileDialog::begin(const std::filesystem::path &location, bool saveMode,
                       std::string context) {
    const char *defaultLocation = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (busy_)
            return false;
        location_ = location.empty() ? std::string{} : location.string();
        context_ = std::move(context);
        saveMode_ = saveMode;
        result_.reset();
        error_.reset();
        busy_ = true;
        defaultLocation = location_.empty() ? nullptr : location_.c_str();
    }
    if (saveMode)
        SDL_ShowSaveFileDialog(callback, this, window_, filters, 2, defaultLocation);
    else
        SDL_ShowOpenFileDialog(callback, this, window_, filters, 2, defaultLocation, false);
    return true;
}

bool FileDialog::open(const std::filesystem::path &directory, std::string context) {
    return begin(directory, false, std::move(context));
}

bool FileDialog::save(const std::filesystem::path &suggested, std::string context) {
    return begin(suggested, true, std::move(context));
}

bool FileDialog::busy() const noexcept {
    std::lock_guard lock(mutex_);
    return busy_;
}

std::optional<FileDialogResult> FileDialog::takeResult() {
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}

std::optional<std::string> FileDialog::takeError() {
    std::lock_guard lock(mutex_);
    auto error = std::move(error_);
    error_.reset();
    return error;
}

void SDLCALL FileDialog::callback(void *userdata, const char *const *files, int) {
    auto &dialog = *static_cast<FileDialog *>(userdata);
    std::lock_guard lock(dialog.mutex_);
    if (files == nullptr)
        dialog.error_ = SDL_GetError();
    else if (files[0] != nullptr)
        dialog.result_ = FileDialogResult{std::filesystem::path(files[0]), dialog.saveMode_, false,
                                          dialog.context_};
    else
        dialog.result_ = FileDialogResult{{}, dialog.saveMode_, true, dialog.context_};
    dialog.busy_ = false;
}

} // namespace pmxer
