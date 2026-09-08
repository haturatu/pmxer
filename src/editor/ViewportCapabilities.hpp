#pragma once

#include "Selection.hpp"

namespace pmxer {

class DocumentSession;

struct TransformCapabilities {
    bool move{};
    bool rotate{};
    bool scale{};

    [[nodiscard]] bool supports(ViewportTool tool) const noexcept;
};

[[nodiscard]] TransformCapabilities transformCapabilities(const DocumentSession &session) noexcept;

} // namespace pmxer
