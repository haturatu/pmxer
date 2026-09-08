#include "Selection.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace pmxer {

void SelectionState::clear() noexcept {
    items_.clear();
}

void SelectionState::set(SelectionItem item) {
    items_.clear();
    items_.push_back(item);
}

void SelectionState::set(std::vector<SelectionItem> items) {
    std::set<SelectionItem> unique;
    items.erase(std::remove_if(items.begin(), items.end(), [&](const auto &item) {
                    return !unique.insert(item).second;
                }),
                items.end());
    items_ = std::move(items);
}

void SelectionState::add(SelectionItem item) {
    if (!contains(item))
        items_.push_back(item);
}

void SelectionState::add(const std::vector<SelectionItem> &items) {
    std::set<SelectionItem> unique(items_.begin(), items_.end());
    for (const auto &item : items)
        if (unique.insert(item).second)
            items_.push_back(item);
}

void SelectionState::toggle(const std::vector<SelectionItem> &items) {
    const std::set<SelectionItem> toggled(items.begin(), items.end());
    std::set<SelectionItem> existing(items_.begin(), items_.end());
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const auto &item) { return toggled.contains(item); }),
                 items_.end());
    for (const auto &item : toggled)
        if (!existing.contains(item))
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
