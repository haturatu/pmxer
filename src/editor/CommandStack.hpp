#pragma once

#include <mmd/document.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pmxer {

class EditorCommand {
  public:
    virtual ~EditorCommand() = default;
    [[nodiscard]] virtual bool apply(mmd::PmxDocument &document) = 0;
    [[nodiscard]] virtual bool undo(mmd::PmxDocument &document) = 0;
    [[nodiscard]] virtual const std::string &description() const noexcept = 0;
};

class CommandStack {
  public:
    void recordApplied(std::unique_ptr<EditorCommand> command);
    [[nodiscard]] bool undo(mmd::PmxDocument &document);
    [[nodiscard]] bool redo(mmd::PmxDocument &document);
    void clear() noexcept;
    void markClean() noexcept;
    void markDirty() noexcept;
    [[nodiscard]] bool isModified() const noexcept;
    [[nodiscard]] std::size_t undoCount() const noexcept;
    [[nodiscard]] std::size_t redoCount() const noexcept;

  private:
    struct Entry {
        std::unique_ptr<EditorCommand> command;
        std::uint64_t beforeState{};
        std::uint64_t afterState{};
    };
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    std::uint64_t nextState_{1};
    std::uint64_t currentState_{};
    std::uint64_t cleanState_{};
};

} // namespace pmxer
