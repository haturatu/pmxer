#pragma once
#include "editor/AiEditExecutor.hpp"
namespace pmxer::editing {
// Face order and snapshot domains do not define connectivity.
bool sameConnectivity(const mmd::PmxDocument &, const mmd::PmxDocument &);
void validateEditGuards(const DocumentSession &source, const DocumentSession &preview,
                        const ai::EditRegion &, const nlohmann::json &, const ai::EditTrace &);
} // namespace pmxer::editing
