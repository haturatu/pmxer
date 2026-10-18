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
        bad["operations"][0]["detail"] = std::uint64_t{4294967320};
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
            require(acceptEditPreview(source, captured, *preview).success &&
                        source.ui.isolatedMaterials.size() == 2,
                    "Applied generation disappeared from isolated view");
            for (auto h : source.changes.vertices)
                require(h.domain == source.document.domain() && source.document.resolve(h),
                        "Accepted preview published foreign vertex handles");
        }
        for (auto shape : {Shape::loafers, Shape::shirt, Shape::skirt, Shape::dress, Shape::pants,
                           Shape::boots, Shape::coat}) {
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
        {
            auto model = sample();
            model.materials[0].name = "髪";
            model.materials[0].indexCount = 3;
            auto clothing = model.materials[0];
            clothing.name = "スカート";
            model.materials.push_back(clothing);
            DocumentSession mixed(model);
            const auto capture = captureFaces(mixed, std::array<std::size_t, 2>{0, 1});
            const auto context = regionContext(mixed, capture);
            require(context["materials"].size() == 2 && context["materials"][1]["index"] == 1 &&
                        context["materials"][1]["selectedFaces"] == 1 &&
                        context["materials"][1].contains("influencingBones"),
                    "Rough selection lacks per-material targeting context");
            auto scoped = parsePlan(j.dump());
            scoped.target.materialIndices = {1};
            Operation subdivision;
            subdivision.type = OperationType::subdivide;
            scoped.operations.insert(scoped.operations.begin(), subdivision);
            const auto protectedFace = mixed.document.faceHandle(0);
            const auto protectedValue = *mixed.document.resolve(protectedFace);
            const auto preview = makeEditPreview(mixed, capture, scoped);
            require(preview->document.faces().size() == 5 && mixed.document.faces().size() == 2,
                    "Scoped mesh preview changed the source");
            require(applyEditPlan(mixed, capture, scoped).success, "Rough scoped edit failed");
            require(mixed.document.faces().size() == 5 &&
                        *mixed.document.resolve(protectedFace) == protectedValue,
                    "Unrelated hair was edited during clothing change");
            for (auto h : protectedValue.vertices)
                require(mixed.document.resolve(h)->position == model.vertices[h.id - 1].position,
                        "Unrelated vertex moved");
            require(mixed.commands.undoCount() == 1 && mixed.undo() &&
                        mmd::pmx::semanticCompare(model, mixed.document.model()).equal() &&
                        mixed.redo(),
                    "Combined edit did not undo atomically");
            require(mixed.undo(), "Reset rough edit failed");
            const auto fresh = captureFaces(mixed, std::array<std::size_t, 2>{0, 1});
            auto invalid = scoped;
            invalid.target.materialIndices = {99};
            require(!applyEditPlan(mixed, fresh, invalid).success, "Nonexistent target accepted");
            const auto single = captureFaces(mixed, std::array<std::size_t, 1>{0});
            invalid.target.materialIndices = {1};
            require(!applyEditPlan(mixed, single, invalid).success, "Target escaped capture");
            invalid = scoped;
            invalid.target.maximum[1] = 0;
            require(!applyEditPlan(mixed, fresh, invalid).success, "Empty target accepted");
            auto lateFailure = scoped;
            lateFailure.operations[1].amount = 11;
            require(!applyEditPlan(mixed, fresh, lateFailure).success &&
                        mmd::pmx::semanticCompare(model, mixed.document.model()).equal(),
                    "Failed combined plan partially modified the model");
            auto replacement = scoped;
            replacement.operations[0].type = OperationType::generate;
            replacement.operations[0].shape = Shape::dress;
            require(applyEditPlan(mixed, fresh, replacement).success &&
                        *mixed.document.resolve(protectedFace) == protectedValue,
                    "Generated dress edited unrelated parts");
        }
        {
            auto model = sample();
            model.vertices[0].uv = {0, 0};
            model.vertices[1].uv = {1, 0};
            model.metadata.additionalUvCount = 1;
            model.vertices[1].additionalUv[0] = {1, 2, 3, 4};
            mmd::PmxMorph morph;
            morph.name = "Vertex morph";
            morph.type = 1;
            mmd::PmxMorphOffset offset;
            offset.index = 0;
            offset.vector3 = {0, 2, 0};
            morph.offsets.push_back(offset);
            model.morphs.push_back(morph);
            morph.name = "UV morph";
            morph.type = 3;
            morph.offsets[0].vector4 = {0.4F, 0, 0, 0};
            model.morphs.push_back(morph);
            DocumentSession mesh(model);
            const auto capture = captureFaces(mesh, std::array<std::size_t, 2>{0, 1});
            Operation subdivide;
            subdivide.type = OperationType::subdivide;
            require(applyEditPlan(mesh, capture, EditPlan{"Upscale", {subdivide}}).success,
                    "Subdivision failed");
            require(mesh.document.faces().size() == 8 && mesh.document.model().vertices.size() == 9,
                    "Subdivision did not reuse shared edge midpoints");
            std::size_t midpointIndex = 0;
            for (std::size_t i = 4; i < mesh.document.model().vertices.size(); ++i) {
                const auto &v = mesh.document.model().vertices[i];
                if (v.position == mmd::Float3{0, 0, -1})
                    midpointIndex = i;
            }
            require(midpointIndex >= 4, "Expected midpoint missing");
            const auto &middle = mesh.document.model().vertices[midpointIndex];
            require(middle.uv == mmd::Float2{0.5F, 0} &&
                        middle.additionalUv[0] == mmd::Float4{0.5F, 1, 1.5F, 2} &&
                        middle.bones[0] != middle.bones[1] && middle.weights[0] == 0.5F &&
                        middle.weights[1] == 0.5F,
                    "Subdivision lost UVs or bone weights");
            bool vertexMorph = false, uvMorph = false;
            for (const auto &o : mesh.document.model().morphs[0].offsets)
                if (o.index == static_cast<std::int32_t>(midpointIndex))
                    vertexMorph = o.vector3 == mmd::Float3{0, 1, 0};
            for (const auto &o : mesh.document.model().morphs[1].offsets)
                if (o.index == static_cast<std::int32_t>(midpointIndex))
                    uvMorph = o.vector4 == mmd::Float4{0.2F, 0, 0, 0};
            require(vertexMorph && uvMorph && mesh.document.validate().valid(),
                    "Subdivision lost morph interpolation");
            require(mesh.undo() && mmd::pmx::semanticCompare(model, mesh.document.model()).equal(),
                    "Subdivision undo lost original data");
            subdivide.iterations = 2;
            require(applyEditPlan(mesh, captureFaces(mesh, std::array<std::size_t, 2>{0, 1}),
                                  EditPlan{"Upscale twice", {subdivide}})
                            .success &&
                        mesh.document.faces().size() == 32 && mesh.commands.undoCount() == 1,
                    "Repeated subdivision failed");
        }
        {
            auto model = sample();
            model.vertices.clear();
            for (const auto &p : std::array<mmd::Float3, 5>{
                     {{-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}, {1, 0, -1}, {0, 0.5F, 0}}}) {
                mmd::PmxVertex vertex;
                vertex.position = p;
                vertex.normal = {0, 1, 0};
                vertex.bones[0] = 0;
                model.vertices.push_back(vertex);
            }
            model.indices = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
            model.materials[0].indexCount = 12;
            DocumentSession mesh(model);
            Operation smooth;
            smooth.type = OperationType::smooth;
            smooth.amount = 0.5F;
            Operation inflate;
            inflate.type = OperationType::inflate;
            inflate.amount = 0.1F;
            const auto chosen = captureFaces(mesh, std::array<std::size_t, 4>{0, 1, 2, 3});
            require(applyEditPlan(mesh, chosen, EditPlan{"Smooth and inflate", {smooth, inflate}})
                        .success,
                    "Smoothing and inflation failed");
            require(std::abs(mesh.document.model().vertices[4].position[1] - 0.35F) < 1e-5F,
                    "Smoothing or inflation has no effect");
            for (std::size_t i = 0; i < 4; ++i)
                require(mesh.document.model().vertices[i] == model.vertices[i],
                        "Deformation moved boundary vertices");
            require(mesh.commands.undoCount() == 1 && mesh.undo() &&
                        mmd::pmx::semanticCompare(model, mesh.document.model()).equal(),
                    "Deformation undo failed");
        }
        {
            DocumentSession garment(sample());
            auto rough = captureFaces(garment, std::array<std::size_t, 1>{0});
            auto plan = parsePlan(j.dump());
            plan.target.materialIndices = {0};
            plan.target.completeMaterials = true;
            require(!applyEditPlan(garment, rough, plan).success,
                    "Material completion escaped strict capture");
            rough.allowMaterialExpansion = true;
            require(resolveTarget(garment, rough, plan.target).faces.size() == 2,
                    "Rough selection did not complete the garment");
            const auto preview = makeEditPreview(garment, rough, plan);
            require(preview->document.model().materials.size() == 2 &&
                        garment.document.model().materials.size() == 1,
                    "Completed preview modified the source");
            require(applyEditPlan(garment, rough, plan).success, "Completed material edit failed");
            for (const auto &face : garment.document.faces())
                require(face.material != garment.document.materialHandle(0),
                        "Garment back side was left unchanged");
            plan.target.materialIndices.clear();
            require(garment.undo() &&
                        !applyEditPlan(garment,
                                       captureFaces(garment, std::array<std::size_t, 1>{0}), plan)
                             .success,
                    "Material completion without explicit targets was accepted");
        }
        {
            auto valid = j;
            valid["target"] = {
                {"materialIndices", {0}}, {"minimum", {0, 0, 0}}, {"maximum", {1, 1, 1}}};
            valid["operations"][0]["type"] = "subdivide";
            valid["operations"][0]["iterations"] = 2;
            require(parsePlan(valid.dump()).target.materialIndices == std::vector<std::size_t>{0},
                    "Target parsing failed");
            auto invalid = valid;
            invalid["target"]["materialIndices"] = {0, 0};
            fails(invalid);
            invalid = valid;
            invalid["target"]["materialIndices"] = {-1};
            fails(invalid);
            invalid = valid;
            invalid["target"]["minimum"] = {0, 2, 0};
            fails(invalid);
            invalid = valid;
            invalid["operations"][0]["iterations"] = 4294967297LL;
            fails(invalid);
            invalid = valid;
            invalid["operations"][0]["type"] = "smooth";
            invalid["operations"][0]["amount"] = -0.1;
            fails(invalid);
            invalid = valid;
            invalid["operations"][0]["type"] = "generate";
            invalid["operations"][0]["shape"] = "dress";
            invalid["operations"].push_back(invalid["operations"][0]);
            fails(invalid);
        }
        {
            auto model = sample();
            model.format.vertexIndexSize = 1;
            model.vertices.resize(257, model.vertices[0]);
            DocumentSession widened(model);
            const auto path = std::filesystem::temp_directory_path() /
                              ("pmxer-ai-width-" + widened.recoveryId + ".pmx");
            const auto result = saveDocument(widened, path);
            require(result.success && result.report.vertex.widened() && result.verification.equal(),
                    "Index width growth prevented saving a denser mesh");
            const auto loaded = mmd::pmx::load(path);
            require(loaded.format.vertexIndexSize == 2 &&
                        mmd::pmx::semanticCompare(model, loaded).equal(),
                    "Widened PMX changed model semantics");
            std::filesystem::remove(path);
        }
        {
            DocumentSession hair(sample());
            Operation lengthen;
            lengthen.type = OperationType::transform;
            lengthen.scale = {1, 2, 1};
            lengthen.pivot = {0.5F, 1, 0.5F};
            require(applyEditPlan(hair, captureFaces(hair, std::array<std::size_t, 2>{0, 1}),
                                  EditPlan{"Lengthen from roots", {lengthen}})
                        .success,
                    "Anchored transform failed");
            require(hair.document.model().vertices[2].position[1] == 1 &&
                        hair.document.model().vertices[0].position[1] == -1,
                    "Anchored transform moved the roots or waistband");
        }
        {
            DocumentSession source(sample());
            const auto captured = captureFaces(source, std::array<std::size_t, 2>{0, 1});
            auto prepared = prepareEditPreview(source);
            const auto privateRegion = captureFaces(*prepared, captured.faceIndices);
            auto edit = parsePlan(j.dump());
            require(applyEditPlan(source, captured, edit).success &&
                        applyEditPlan(*prepared, privateRegion, edit).success,
                    "Independent background preparation failed");
            const auto changed = source.document.model();
            require(!acceptEditPreview(source, captured, *prepared).success &&
                        mmd::pmx::semanticCompare(changed, source.document.model()).equal(),
                    "Stale background preview overwrote newer edits");
            DocumentSession foreign(sample());
            require(!acceptEditPreview(foreign,
                                       captureFaces(foreign, std::array<std::size_t, 2>{0, 1}),
                                       *prepared)
                         .success,
                    "Preview from another document was accepted");
        }
        {
            auto many = sample();
            many.indices.clear();
            many.materials.clear();
            many.bones[0].name = std::string(256, '\1');
            many.bones[1].name = many.bones[0].name;
            for (int i = 0; i < 256; ++i) {
                auto material = sample().materials[0];
                material.name = std::string(256, '\1');
                material.englishName = material.name;
                many.materials.push_back(material);
                many.indices.insert(many.indices.end(), {0, 1, 2, 0, 2, 3});
            }
            DocumentSession session(many);
            std::vector<std::size_t> indices;
            for (std::size_t i = 0; i < session.document.faces().size(); ++i)
                indices.push_back(i);
            const auto context = regionContext(session, captureFaces(session, indices));
            require(context.dump().size() <= 65536 && context["materialsTruncated"] == true,
                    "Material context exceeds the Codex request budget");
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
