#pragma once
#include "AiEditExecutor.hpp"

namespace pmxer::ai {
// Called only on the private staging document; throws before publishing on
// failure.
void editMesh(mmd::PmxDocument &, mmd::PmxDocument::Transaction &, const EditRegion &,
              const Operation &);
} // namespace pmxer::ai
