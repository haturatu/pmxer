#pragma once

#include <mmd/document.hpp>

#include <cstdint>
#include <vector>

namespace pmxer {

enum class SelectionKind : std::uint8_t {
    vertex,
    texture,
    material,
    bone,
    morph,
    displayFrame,
    rigidBody,
    joint,
    softBody,
    face,
};

struct SelectionItem {
    SelectionKind kind{};
    std::uint64_t id{};
    std::uint32_t generation{};
    auto operator<=>(const SelectionItem &) const = default;
};

class SelectionState {
  public:
    void clear() noexcept;
    void set(SelectionItem item);
    void add(SelectionItem item);
    void remove(SelectionItem item);
    [[nodiscard]] bool contains(SelectionItem item) const noexcept;
    [[nodiscard]] const std::vector<SelectionItem> &items() const noexcept;

  private:
    std::vector<SelectionItem> items_;
};

} // namespace pmxer

