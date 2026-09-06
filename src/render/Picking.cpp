#include "Picking.hpp"

namespace pmxer {

std::uint32_t PickingTable::assign(SelectionItem item) {
    if (next_ == 0)
        clear();
    const auto id = next_++;
    values_[id] = item;
    return id;
}

std::optional<SelectionItem> PickingTable::resolve(std::uint32_t id) const {
    const auto found = values_.find(id);
    return found == values_.end() ? std::nullopt : std::optional<SelectionItem>(found->second);
}

void PickingTable::clear() noexcept {
    values_.clear();
    next_ = 1;
}

} // namespace pmxer

