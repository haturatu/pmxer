#include "ai/EditPlan.hpp"
#include "editor/AiEditExecutor.hpp"
#include "editor/SaveController.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mmd/pmx.hpp>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
mmd::PmxModel sample() {
    mmd::PmxModel m;
    m.metadata.version = 2.1F;
    m.metadata.modelName = "AI regression";
    mmd::PmxBone left;
    left.name = "左足首";
    m.bones.push_back(left);
    mmd::PmxBone right;
    right.name = "右足首";
    m.bones.push_back(right);
    for (auto p : std::array<mmd::Float3, 4>{{{-1, 0, -1}, {1, 0, -1}, {1, 1, 1}, {-1, 1, 1}}}) {
        mmd::PmxVertex v;
        v.position = p;
        v.normal = {0, 1, 0};
        v.bones[0] = p[0] < 0 ? 0 : 1;
        m.vertices.push_back(v);
    }
    m.indices = {0, 1, 2, 0, 2, 3};
    mmd::PmxMaterial material;
    material.name = "Shared";
    material.diffuse = {0.7F, 0.7F, 0.7F, 1};
    material.indexCount = 6;
    m.materials.push_back(material);
    return m;
}
nlohmann::json planJson() {
    return {{"version", 1},
            {"summary", "Test edit"},
            {"operations", nlohmann::json::array({{{"type", "recolor"},
                                                   {"shape", "none"},
                                                   {"color", {0.2, 0.3, 0.4, 1.0}},
                                                   {"translation", {0, 0, 0}},
                                                   {"scale", {1, 1, 1}},
                                                   {"rotation", {0, 0, 0}},
                                                   {"replace", false},
                                                   {"detail", 24}}})}};
}
void fails(const nlohmann::json &j) {
    try {
        (void)pmxer::ai::parsePlan(j.dump());
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid plan accepted");
}
} // namespace
int main() {
    try {
        using namespace pmxer;
        using namespace pmxer::ai;
        auto j = planJson();
        require(parsePlan(j.dump()).operations.size() == 1, "Valid plan rejected");
        auto bad = j;
        bad["operations"][0]["type"] = "shell";
        fails(bad);
        bad = j;
        bad["operations"][0]["color"] = {-1, 1, 1, 1};
        fails(bad);
        bad = j;
        bad["operations"][0]["path"] = "model.pmx";
        fails(bad);
        bad = j;
        bad["operations"][0]["scale"] = {0, 1, 1};
        fails(bad);
        bad = j;
        bad["operations"][0]["detail"] = 256;
        fails(bad);
        bad = j;
        bad["version"] = 2;
        fails(bad);
        bad = j;
        bad["operations"][0]["translation"] = {1e200, 0, 0};
        fails(bad);
        bad = j;
        bad["operations"][0]["shape"] = "shirt";
        fails(bad);
        DocumentSession s(sample());
        const auto outside = s.document.faceHandle(1);
        const auto originalFace = *s.document.resolve(outside);
        auto region = captureFaces(s, std::array<std::size_t, 1>{0});
        const auto context = regionContext(s, region);
        require(context["selectedFaces"] == 1 && !context.contains("path"),
                "Unexpected request context");
        const auto base = s.document.model();
        require(applyEditPlan(s, region, parsePlan(j.dump())).success, "Recolor failed");
        require(s.document.model().materials.size() == 2, "Partial material was not isolated");
        require(s.document.resolve(outside)->material == originalFace.material,
                "Outside face material changed");
        require(s.document.model().materials[0].diffuse == base.materials[0].diffuse,
                "Shared material changed");
        require(s.commands.undoCount() == 1, "AI operation must be one undo step");
        require(s.undo() && mmd::pmx::semanticCompare(base, s.document.model()).equal(),
                "Recolor undo failed");
        require(!applyEditPlan(s, region, parsePlan(j.dump())).success, "Stale plan was applied");
        require(s.redo() && s.undo(), "Recolor redo failed");
        region = captureFaces(s, std::array<std::size_t, 1>{0});
        auto transform = parsePlan(j.dump());
        transform.operations[0].type = OperationType::transform;
        transform.operations[0].translation = {0, 0.25F, 0};
        require(applyEditPlan(s, region, transform).success, "Partial transform failed");
        for (auto v : originalFace.vertices) {
            const auto index = static_cast<std::size_t>(v.id - 1);
            require(s.document.resolve(v)->position == base.vertices[index].position,
                    "Outside seam vertex moved");
        }
        require(s.document.model().vertices.size() == 6, "Boundary vertices were not split");
        const auto transformed = s.document.model();
        // A later edit must keep stable handles valid across snapshot undo/redo.
        auto nextRegion = captureFaces(s, std::array<std::size_t, 1>{0});
        require(applyEditPlan(s, nextRegion, parsePlan(j.dump())).success,
                "Chained recolor failed");
        const auto recolored = s.document.model();
        require(s.undo() && mmd::pmx::semanticCompare(transformed, s.document.model()).equal(),
                "Chained undo lost the previous edit");
        require(s.undo() && mmd::pmx::semanticCompare(base, s.document.model()).equal(),
                "Transform undo failed");
        require(s.redo() && s.redo() &&
                    mmd::pmx::semanticCompare(recolored, s.document.model()).equal() &&
                    s.document.resolve(outside) != nullptr,
                "Chained redo lost model data or stable handles");
        {
            auto model = sample();
            model.materials[0].indexCount = 3;
            model.materials.push_back(model.materials[0]);
            DocumentSession source(model);
            const auto isolated = source.document.materialHandle(0);
            const auto hidden = source.document.materialHandle(1);
            source.ui.isolatedMaterials = {
                {SelectionKind::material, isolated.domain, isolated.id, isolated.generation}};
            source.ui.hiddenMaterials = {
                {SelectionKind::material, hidden.domain, hidden.id, hidden.generation}};
            mmd::AnimatedModelFrame oldFrame;
            oldFrame.vertices.resize(1);
            source.ui.previewFrame = &oldFrame;
            const auto captured = captureFaces(source, std::array<std::size_t, 1>{0});
            Operation op;
            op.type = OperationType::generate;
            op.shape = Shape::skirt;
            const EditPlan plan{"Preview skirt", {op}};
            const auto preview = makeEditPreview(source, captured, plan);
            require(preview->document.domain() != source.document.domain() &&
                        preview->ui.aiPreviewReadOnly && preview->ui.previewFrame == nullptr,
                    "Preview borrowed the source document or animation frame");
            require(preview->ui.hiddenMaterials[0].domain == preview->document.domain() &&
                        preview->document.resolve(selectionHandle<mmd::MaterialTag>(
                            preview->document, preview->ui.hiddenMaterials[0])) != nullptr,
                    "Preview lost material visibility handles");
            require(preview->ui.isolatedMaterials.size() == 2 &&
                        preview->ui.isolatedMaterials.back().domain == preview->document.domain(),
                    "Generated material was hidden by source isolation");
            require(mmd::pmx::semanticCompare(model, source.document.model()).equal() &&
                        source.commands.undoCount() == 0 && source.ui.isolatedMaterials.size() == 1,
                    "Preview modified the source model or its view");
            require(applyEditPlan(source, captured, plan).success &&
                        source.ui.isolatedMaterials.size() == 2,
                    "Applied generation disappeared from isolated view");
        }
        for (auto shape : {Shape::loafers, Shape::shirt, Shape::skirt}) {
            DocumentSession generated(sample());
            auto chosen = captureFaces(generated, std::array<std::size_t, 1>{0});
            EditPlan plan{"Generate test", {}};
            Operation op;
            op.type = OperationType::generate;
            op.shape = shape;
            op.replace = true;
            plan.operations.push_back(op);
            const auto untouched = generated.document.faceHandle(1);
            const auto result = applyEditPlan(generated, chosen, plan);
            if (!result.success)
                throw std::runtime_error("Generation: " + result.message);
            require(generated.document.resolve(untouched) != nullptr, "Outside face removed");
            require(generated.document.model().vertices.size() > 100, "Geometry was not generated");
            require(generated.document.validate().valid(), "Generated PMX is invalid");
            for (const auto &v : generated.document.model().vertices) {
                for (auto p : v.position)
                    require(std::isfinite(p), "Non-finite position");
                float sum = 0;
                for (auto w : v.weights)
                    sum += w;
                require(std::abs(sum - 1) < 1e-4F, "Weights not normalized");
            }
            const auto model = generated.document.model();
            require(generated.undo() &&
                        mmd::pmx::semanticCompare(base, generated.document.model()).equal(),
                    "Generation undo failed");
            require(generated.redo() &&
                        mmd::pmx::semanticCompare(model, generated.document.model()).equal(),
                    "Generation redo failed");
            const auto path = std::filesystem::temp_directory_path() /
                              ("pmxer-ai-test-" + generated.recoveryId + ".pmx");
            const auto saved = saveDocument(generated, path);
            if (!saved.success)
                throw std::runtime_error("Save: " + saved.message);
            require(mmd::pmx::semanticCompare(model, mmd::pmx::load(path)).equal(),
                    "Saved generation did not round-trip");
            std::filesystem::remove(path);
        }
        DocumentSession changed(sample());
        auto frozen = captureFaces(changed, std::array<std::size_t, 1>{0});
        changed.deform.dirty = true;
        require(!applyEditPlan(changed, frozen, parsePlan(j.dump())).success,
                "Pending transform was ignored");
        changed.deform.dirty = false;
        auto foreign = frozen;
        foreign.domain++;
        require(!applyEditPlan(changed, foreign, parsePlan(j.dump())).success,
                "Foreign selection accepted");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
