#pragma once

#include "../editor/Selection.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace pmxer {

class PickingTable {
  public:
    [[nodiscard]] std::uint32_t assign(SelectionItem item);
    [[nodiscard]] std::optional<SelectionItem> resolve(std::uint32_t id) const;
    void clear() noexcept;

  private:
    std::uint32_t next_{1};
    std::unordered_map<std::uint32_t, SelectionItem> values_;
};

[[nodiscard]] std::uint32_t encodePickingId(SelectionKind kind, std::uint32_t value) noexcept;
[[nodiscard]] SelectionKind decodePickingKind(std::uint32_t id) noexcept;
[[nodiscard]] std::uint32_t decodePickingValue(std::uint32_t id) noexcept;

} // namespace pmxer
