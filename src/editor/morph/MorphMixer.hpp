#pragma once

#include "../DeformSession.hpp"
#include "../EditorOperations.hpp"

#include <mmd/pmx.hpp>

#include <string>

namespace pmxer {

struct DocumentSession;

namespace morph {

void setBlend(DocumentSession &session, mmd::MorphHandle morph, float weight);
void clearBlend(DocumentSession &session, mmd::MorphHandle morph);
void resetMix(DocumentSession &session);
void setSoloMorph(DocumentSession &session, mmd::MorphHandle morph);
void clearSoloMorph(DocumentSession &session);
void syncPreview(DocumentSession &session);

[[nodiscard]] OperationResult bakeMixAsVertexMorph(DocumentSession &session,
                                                    std::string name);

} // namespace morph
} // namespace pmxer
