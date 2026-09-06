#include "Selection.hpp"

#include <algorithm>

namespace pmxer {

void SelectionState::clear() noexcept {
    items_.clear();
}

void SelectionState::set(SelectionItem item) {
    items_.clear();
    items_.push_back(item);
}

void SelectionState::add(SelectionItem item) {
    if (!contains(item))
        items_.push_back(item);
}

void SelectionState::remove(SelectionItem item) {
    items_.erase(std::remove(items_.begin(), items_.end(), item), items_.end());
}

bool SelectionState::contains(SelectionItem item) const noexcept {
    return std::find(items_.begin(), items_.end(), item) != items_.end();
}

const std::vector<SelectionItem> &SelectionState::items() const noexcept {
    return items_;
}

} // namespace pmxer

