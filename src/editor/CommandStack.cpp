#include "CommandStack.hpp"

namespace pmxer {

void CommandStack::recordApplied(std::unique_ptr<EditorCommand> command) {
    if (!command)
        return;
    undo_.push_back(std::move(command));
    redo_.clear();
}

bool CommandStack::undo(mmd::PmxDocument &document) {
    if (undo_.empty())
        return false;
    auto command = std::move(undo_.back());
    undo_.pop_back();
    if (!command->undo(document)) {
        undo_.push_back(std::move(command));
        return false;
    }
    redo_.push_back(std::move(command));
    return true;
}

bool CommandStack::redo(mmd::PmxDocument &document) {
    if (redo_.empty())
        return false;
    auto command = std::move(redo_.back());
    redo_.pop_back();
    if (!command->apply(document)) {
        redo_.push_back(std::move(command));
        return false;
    }
    undo_.push_back(std::move(command));
    return true;
}

void CommandStack::clear() noexcept {
    undo_.clear();
    redo_.clear();
}

std::size_t CommandStack::undoCount() const noexcept {
    return undo_.size();
}

std::size_t CommandStack::redoCount() const noexcept {
    return redo_.size();
}

} // namespace pmxer

