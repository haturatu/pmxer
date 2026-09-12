#pragma once

#include "Selection.hpp"

#include <mmd/pmx.hpp>

#include <cstddef>
#include <string_view>

namespace pmxer {

struct DocumentSession;

struct TransformCapabilities {
    bool move{};
    bool rotate{};
    bool scale{};

    [[nodiscard]] bool supports(ViewportTool tool) const noexcept;
};

enum class SupportLevel { supported, partial, unsupported };

struct ActionAvailability {
    bool enabled{};
    SupportLevel support{SupportLevel::unsupported};
    std::string_view reason;
};

enum class EditorAction { viewportMove, viewportRotate, viewportScale };

[[nodiscard]] TransformCapabilities transformCapabilities(const DocumentSession &session) noexcept;
[[nodiscard]] bool deformBoneTransformAllowed(const DocumentSession &session,
                                               mmd::BoneHandle bone) noexcept;
[[nodiscard]] ActionAvailability boneTransformAvailability(
    const DocumentSession &session, mmd::BoneHandle bone) noexcept;
[[nodiscard]] ActionAvailability actionAvailability(EditorAction action,
                                                    const DocumentSession &session) noexcept;

} // namespace pmxer
