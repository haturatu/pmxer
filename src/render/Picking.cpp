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

std::uint32_t encodePickingId(SelectionKind kind, std::uint32_t value) noexcept {
    return (static_cast<std::uint32_t>(kind) << 28U) | (value & 0x0fffffffU);
}

SelectionKind decodePickingKind(std::uint32_t id) noexcept {
    return static_cast<SelectionKind>((id >> 28U) & 0x0fU);
}

std::uint32_t decodePickingValue(std::uint32_t id) noexcept {
    return id & 0x0fffffffU;
}

} // namespace pmxer
