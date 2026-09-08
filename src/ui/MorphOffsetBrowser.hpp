#pragma once

#include <mmd/document.hpp>

#include <cstddef>
#include <string_view>

namespace pmxer {

struct DocumentSession;

struct MorphOffsetBrowserResult {
    bool selectionChanged{};
};

[[nodiscard]] MorphOffsetBrowserResult drawMorphOffsetBrowser(
    DocumentSession &session, const mmd::PmxMorph &morph,
    std::size_t &selectedIndex, std::string_view id,
    mmd::MorphHandle morphHandle = {});

} // namespace pmxer
