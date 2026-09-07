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

enum class ViewportSelectionMode : std::uint8_t { vertex, face, material, bone, rigidBody, joint };
enum class ViewportTool : std::uint8_t { select, move, rotate, scale };

struct SelectionItem {
    SelectionKind kind{};
    std::uint64_t domain{};
    std::uint64_t id{};
    std::uint32_t generation{};
    auto operator<=>(const SelectionItem &) const = default;
};

template <typename Tag>
[[nodiscard]] mmd::PmxHandle<Tag> selectionHandle(const mmd::PmxDocument &document,
                                                   const SelectionItem &item) noexcept {
    if (item.domain != document.domain())
        return {};
    return {item.domain, item.id, item.generation};
}

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
