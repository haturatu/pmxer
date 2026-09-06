#include "CommandStack.hpp"

namespace pmxer {

void CommandStack::recordApplied(std::unique_ptr<EditorCommand> command) {
    if (!command)
        return;
    currentState_ = nextState_++;
    undo_.push_back({std::move(command), currentState_});
    redo_.clear();
}

bool CommandStack::undo(mmd::PmxDocument &document) {
    if (undo_.empty())
        return false;
    auto entry = std::move(undo_.back());
    undo_.pop_back();
    if (!entry.command->undo(document)) {
        undo_.push_back(std::move(entry));
        return false;
    }
    currentState_ = undo_.empty() ? 0 : undo_.back().state;
    redo_.push_back(std::move(entry));
    return true;
}

bool CommandStack::redo(mmd::PmxDocument &document) {
    if (redo_.empty())
        return false;
    auto entry = std::move(redo_.back());
    redo_.pop_back();
    if (!entry.command->apply(document)) {
        redo_.push_back(std::move(entry));
        return false;
    }
    currentState_ = entry.state;
    undo_.push_back(std::move(entry));
    return true;
}

void CommandStack::clear() noexcept {
    undo_.clear();
    redo_.clear();
    currentState_ = 0;
    cleanState_ = 0;
}

void CommandStack::markClean() noexcept {
    cleanState_ = currentState_;
}

void CommandStack::markDirty() noexcept {
    currentState_ = nextState_++;
}

bool CommandStack::isModified() const noexcept {
    return currentState_ != cleanState_;
}

std::size_t CommandStack::undoCount() const noexcept {
    return undo_.size();
}

std::size_t CommandStack::redoCount() const noexcept {
    return redo_.size();
}

} // namespace pmxer
