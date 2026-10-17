#pragma once
#include "DocumentSession.hpp"
#include "EditorOperations.hpp"
#include "ai/EditPlan.hpp"
#include <span>

namespace pmxer::ai {
struct EditRegion {
    std::string documentId;
    std::uint64_t domain{};
    std::uint64_t revision{};
    std::vector<mmd::FaceHandle> faces;
    std::vector<std::size_t> faceIndices;
    std::vector<mmd::VertexHandle> vertices;
    mmd::Float3 minimum{}, maximum{};
};
[[nodiscard]] EditRegion captureRegion(const DocumentSession &session);
[[nodiscard]] EditRegion captureFaces(const DocumentSession &session,
                                      std::span<const std::size_t> indices);
[[nodiscard]] nlohmann::json regionContext(const DocumentSession &, const EditRegion &);
[[nodiscard]] OperationResult applyEditPlan(DocumentSession &, const EditRegion &,
                                            const EditPlan &);
} // namespace pmxer::ai
