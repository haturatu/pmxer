#pragma once
#include "DocumentSession.hpp"
#include "EditorOperations.hpp"
#include "ai/EditPlan.hpp"
#include <map>
#include <span>

namespace pmxer::ai {
struct EditTrace {
    // IDs survive private snapshot domain remapping. New vertices inherit from
    // existing vertices or earlier entries in this acyclic interpolation graph.
    std::map<std::uint64_t, std::vector<std::pair<std::uint64_t, float>>> vertexSources;
    std::map<std::uint64_t, std::uint64_t> materialSources;
};
struct EditRegion {
    std::string documentId;
    std::uint64_t domain{};
    std::uint64_t revision{};
    bool allowMaterialExpansion{};
    std::vector<mmd::FaceHandle> faces;
    std::vector<std::size_t> faceIndices;
    std::vector<mmd::VertexHandle> vertices;
    mmd::Float3 minimum{}, maximum{};
};
[[nodiscard]] EditRegion captureRegion(const DocumentSession &session);
[[nodiscard]] EditRegion captureFaces(const DocumentSession &session,
                                      std::span<const std::size_t> indices);
[[nodiscard]] EditRegion resolveTarget(const DocumentSession &, const EditRegion &,
                                       const EditTarget &);
[[nodiscard]] nlohmann::json regionContext(const DocumentSession &, const EditRegion &);
[[nodiscard]] OperationResult applyEditPlan(DocumentSession &, const EditRegion &, const EditPlan &,
                                            EditTrace *trace = nullptr);
[[nodiscard]] OperationResult acceptEditPreview(DocumentSession &, const EditRegion &,
                                                const DocumentSession &);
[[nodiscard]] std::unique_ptr<DocumentSession> prepareEditPreview(const DocumentSession &);
[[nodiscard]] std::unique_ptr<DocumentSession> makeEditPreview(const DocumentSession &,
                                                               const EditRegion &, const EditPlan &,
                                                               EditTrace *trace = nullptr);
} // namespace pmxer::ai
