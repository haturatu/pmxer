#pragma once

#include "../DeformSession.hpp"
#include "../EditorOperations.hpp"
#include "MorphOps.hpp"

#include <mmd/pmx.hpp>

#include <set>
#include <string>
#include <vector>

namespace pmxer {

struct DocumentSession;

namespace morph {

struct VertexBakeAnalysis {
  std::vector<MorphData> vertexParts;
  std::set<std::uint8_t> ignoredTypes;
  bool budgetExceeded{};
};

struct VertexBakeOptions {
  bool allowIgnoredTypes{};
};

[[nodiscard]] std::vector<MorphBlend>
effectiveMorphMix(const DocumentSession &session);
[[nodiscard]] VertexBakeAnalysis
analyzeVertexMix(const DocumentSession &session);
void setBlend(DocumentSession &session, mmd::MorphHandle morph, float weight);
void clearBlend(DocumentSession &session, mmd::MorphHandle morph);
void resetMix(DocumentSession &session);
void setSoloMorph(DocumentSession &session, mmd::MorphHandle morph);
void clearSoloMorph(DocumentSession &session);
void syncPreview(DocumentSession &session);

[[nodiscard]] OperationResult
bakeMixAsVertexMorph(DocumentSession &session, std::string name,
                     VertexBakeOptions options = {});

} // namespace morph
} // namespace pmxer
