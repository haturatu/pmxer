#pragma once

#include "../editor/DocumentSession.hpp"

#include <mmd/animation.hpp>

namespace pmxer {

void drawViewportPanel(DocumentSession &session, const mmd::AnimatedModelFrame *frame = nullptr,
                       bool *open = nullptr);

} // namespace pmxer
