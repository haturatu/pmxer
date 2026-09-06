#pragma once

#include <mmd/document.hpp>

#include <cstddef>
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
    [[nodiscard]] std::size_t undoCount() const noexcept;
    [[nodiscard]] std::size_t redoCount() const noexcept;

  private:
    std::vector<std::unique_ptr<EditorCommand>> undo_;
    std::vector<std::unique_ptr<EditorCommand>> redo_;
};

} // namespace pmxer

