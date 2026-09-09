#pragma once

#include "MorphOps.hpp"

#include "../DocumentSession.hpp"
#include "../EditorOperations.hpp"

#include <string>

namespace pmxer::morph {

[[nodiscard]] OperationResult createMorphFromData(DocumentSession &session,
                                                   MorphData data,
                                                   std::string name,
                                                   std::string description,
                                                   std::uint8_t panel = 0U);
[[nodiscard]] OperationResult captureVertexMorph(DocumentSession &session,
                                                  std::string name);
[[nodiscard]] OperationResult captureBoneMorph(DocumentSession &session,
                                                std::string name);
[[nodiscard]] OperationResult captureGroupMorph(DocumentSession &session,
                                                 std::string name);
[[nodiscard]] OperationResult duplicateMorph(DocumentSession &session,
                                              mmd::MorphHandle source,
                                              std::string name);
[[nodiscard]] OperationResult bakeAndReverseBase(DocumentSession &session,
                                                  mmd::MorphHandle source);
[[nodiscard]] OperationResult createSideSplitMorphs(DocumentSession &session,
                                                    MorphData left,
                                                    MorphData right,
                                                    std::string name);

} // namespace pmxer::morph
