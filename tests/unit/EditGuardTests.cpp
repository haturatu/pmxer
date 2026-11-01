#include "editing/EditGuards.hpp"
#include "editing/EditPipeline.hpp"
#include "editor/SaveController.hpp"
#include "platform/AtomicFile.hpp"
#include "preview/PreviewController.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
using Json = nlohmann::json;
using namespace pmxer;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 1e-5F; }
mmd::PmxModel fixture(bool sharedMaterial = false) {
    mmd::PmxModel m;
    m.metadata.version = 2.1F;
    mmd::PmxBone bone;
    bone.name = "arm";
    m.bones.push_back(bone);
    bone.name = "other arm";
    m.bones.push_back(bone);
    m.textures = {{"diffuse.png"}, {"sphere.png"}, {"toon.png"}};
    for (int t = 0; t < 2; ++t) {
        for (auto p : std::array<mmd::Float3, 3>{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}) {
            mmd::PmxVertex v;
            v.position = p;
            v.position[0] += static_cast<float>(t) * 2;
            v.normal = {0, 0, 1};
            v.uv = {p[0], p[1]};
            v.bones[0] = p[0] > 0 ? 1 : 0;
            m.indices.push_back(static_cast<std::uint32_t>(m.vertices.size()));
            m.vertices.push_back(v);
        }
        if (!t || !sharedMaterial) {
            mmd::PmxMaterial mat;
            mat.name = t ? "other" : "cloth";
            mat.diffuse = {0.4F, 0.5F, 0.6F, 1};
            mat.indexCount = sharedMaterial ? 6U : 3U;
            mat.textureIndex = 0;
            mat.sphereTextureIndex = 1;
            mat.sphereMode = 1;
            mat.toonMode = 0;
            mat.toonTextureIndex = 2;
            m.materials.push_back(mat);
        }
    }
    mmd::PmxMorph material;
    material.name = "tint";
    material.type = 8;
    material.panel = 4;
    mmd::PmxMorphOffset add;
    add.index = 0;
    add.operation = 1;
    add.materialVectors[0] = {0.1F, 0.2F, 0.3F, 0};
    add.materialVectors[4] = {0.2F, 0.1F, 0, 0};
    material.offsets.push_back(add);
    mmd::PmxMorphOffset all;
    all.index = -1;
    all.operation = 0;
    all.materialVectors.fill({1, 1, 1, 1});
    all.materialVectors[0] = {0.5F, 0.6F, 0.7F, 1};
    material.offsets.push_back(all);
    add.materialVectors[0] = {0.03F, 0.02F, 0.01F, 0};
    material.offsets.push_back(add);
    m.morphs.push_back(material);
    mmd::PmxMorph vertex;
    vertex.name = "shape";
    vertex.type = 1;
    vertex.panel = 4;
    mmd::PmxMorphOffset v;
    v.index = 0;
    v.vector3 = {0.2F, 0.3F, 0.4F};
    vertex.offsets.push_back(v);
    m.morphs.push_back(vertex);
    vertex.name = "uv";
    vertex.type = 3;
    vertex.offsets[0].vector4 = {0.1F, 0.2F, 0, 0};
    m.morphs.push_back(vertex);
    return m;
}
Json edit(Json operation) {
    return {{"schema_version", 1},
            {"selection", {{"type", "material"}, {"names", {"cloth"}}, {"expected_count", 1}}},
            {"operations", Json::array({operation})}};
}
void reject(DocumentSession &source, Json plan) {
    try {
        (void)editing::prepareEdit(source, plan);
    } catch (const editing::PlanError &) {
        return;
    }
    throw std::runtime_error("Invalid operation accepted");
}
mmd::AnimatedModelFrame evaluate(const mmd::PmxModel &model, float weight) {
    PreviewController preview(model);
    preview.setPhysicsEnabled(false);
    preview.setMorphPreview("tint", weight);
    return preview.evaluate();
}
int main() {
    std::filesystem::path dir;
    try {
        DocumentSession source(fixture());
        const auto color = mmd::Float4{0.8F, 0.6F, 0.3F, 1};
        auto result = editing::prepareEdit(source, edit({{"type", "recolor"}, {"color", color}}));
        const auto &model = result.preview->document.model();
        check(model.materials.size() == 2, "Whole-material recolor cloned material");
        check(model.materials[0].textureIndex == 0 && model.materials[0].sphereTextureIndex == 1 &&
                  model.materials[0].sphereMode == 1 && model.materials[0].toonTextureIndex == 2,
              "Recolor removed textures");
        check(result.report["topology_changed"] == false, "Recolor reported topology change");
        check(model.morphs == source.document.model().morphs, "Whole recolor changed morph data");
        for (float weight : {0.0F, 0.25F, 1.0F}) {
            auto frame = evaluate(model, weight);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto scale =
                    source.document.model().morphs[0].offsets[1].materialVectors[0][axis];
                const auto first =
                    source.document.model().morphs[0].offsets[0].materialVectors[0][axis];
                const auto last =
                    source.document.model().morphs[0].offsets[2].materialVectors[0][axis];
                check(near(frame.materials[0].diffuse[axis],
                           (color[axis] + first * weight) * (1 + (scale - 1) * weight) +
                               last * weight),
                      "Material morph behavior changed after recolor");
            }
        }
        DocumentSession partial(fixture(true));
        auto region = ai::captureFaces(partial, std::array<std::size_t, 1>{0});
        ai::EditTrace trace;
        ai::Operation recolor;
        recolor.type = ai::OperationType::recolor;
        recolor.color = color;
        auto preview =
            ai::makeEditPreview(partial, region, {"Partial recolor", {recolor}, {}}, &trace);
        editing::validateEditGuards(partial, *preview, region, Json::object(), trace);
        check(preview->document.model().materials.size() == 2,
              "Partial recolor did not split material");
        check(editing::sameConnectivity(partial.document, preview->document),
              "Face reorder changed connectivity");
        const auto &clone = preview->document.model().materials[1];
        check(clone.textureIndex == 0 && clone.sphereMode == 1, "Clone lost texture references");
        for (float weight : {0.0F, 0.25F, 1.0F}) {
            const auto original = evaluate(partial.document.model(), weight);
            const auto changed = evaluate(preview->document.model(), weight);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                check(near(original.materials[0].diffuse[axis], changed.materials[0].diffuse[axis]),
                      "Unselected material morph changed");
                const auto &offsets = partial.document.model().morphs[0].offsets;
                const auto expected = (color[axis] + offsets[0].materialVectors[0][axis] * weight) *
                                          (1 + (offsets[1].materialVectors[0][axis] - 1) * weight) +
                                      offsets[2].materialVectors[0][axis] * weight;
                check(near(expected, changed.materials[1].diffuse[axis]),
                      "Cloned material morph order changed");
                check(near(original.materials[0].textureAdd[axis],
                           changed.materials[1].textureAdd[axis]),
                      "Texture morph effect was lost");
            }
        }
        {
            auto tx = preview->document.transaction();
            check(tx.eraseMorphOffset(preview->document.morphHandle(0), 1) && tx.commit().committed,
                  "Could not construct missing morph clone regression");
            try {
                editing::validateEditGuards(partial, *preview, region, Json::object(), trace);
                throw std::runtime_error("Missing clone morph effect was accepted");
            } catch (const editing::PlanError &e) {
                check(e.code == "guard_failed", "Wrong material morph guard error");
            }
            editing::validateEditGuards(partial, *preview, region, {{"preserve_morphs", false}},
                                        trace);
        }
        {
            auto tx = preview->document.transaction();
            check(tx.setMaterialDiffuse(preview->document.materialHandle(0), {1, 0, 0, 1}) &&
                      tx.commit().committed,
                  "Could not construct shared material regression");
            try {
                editing::validateEditGuards(partial, *preview, region, {{"preserve_morphs", false}},
                                            trace);
                throw std::runtime_error("Unselected material appearance change accepted");
            } catch (const editing::PlanError &e) {
                check(e.code == "guard_failed", "Wrong unselected appearance error");
            }
        }
        auto subdivision =
            editing::prepareEdit(source, edit({{"type", "subdivide"}, {"iterations", 2}}));
        check(subdivision.preview->document.faces().size() == 17,
              "Repeated guarded subdivision failed");
        const auto outside = source.document.faceHandle(1);
        auto remapped = outside;
        remapped.domain = subdivision.preview->document.domain();
        const auto *outsideAfter = subdivision.preview->document.resolve(remapped);
        check(outsideAfter != nullptr, "Subdivision removed unselected face");
        for (std::size_t i = 3; i < 6; ++i)
            check(source.document.model().vertices[i] ==
                      subdivision.preview->document.model().vertices[i],
                  "Subdivision changed unselected vertex");
        for (const auto &op : Json::array(
                 {Json{{"type", "recolor"}, {"translation", {0, 1, 0}}}, Json{{"type", "recolor"}},
                  Json{{"type", "transform"}, {"pivot", "center"}}, Json{{"type", "smooth"}},
                  Json{{"type", "inflate"}}, Json{{"type", "subdivide"}, {"color", {1, 1, 1, 1}}},
                  Json{{"type", "generate"}, {"shape", "shirt"}}}))
            reject(source, edit(op));
        auto corrupted = *outsideAfter;
        auto tx = subdivision.preview->document.transaction();
        auto vertex = corrupted.vertices[0];
        auto value = *subdivision.preview->document.resolve(vertex);
        value.position[0] += 1;
        check(tx.setVertex(vertex, value) && tx.commit().committed,
              "Could not construct guard regression");
        auto selected = ai::captureFaces(source, std::array<std::size_t, 1>{0});
        try {
            editing::validateEditGuards(source, *subdivision.preview, selected, Json::object(), {});
            throw std::runtime_error("Guard accepted modified unselected vertex");
        } catch (const editing::PlanError &e) {
            check(e.code == "guard_failed", "Wrong guard failure");
        }
        {
            ai::Operation subdivide;
            subdivide.type = ai::OperationType::subdivide;
            ai::EditTrace audit;
            auto trial =
                ai::makeEditPreview(source, selected, {"Guard audit", {subdivide}, {}}, &audit);
            auto added = trial->document.vertexHandle(source.document.model().vertices.size());
            auto value = *trial->document.resolve(added);
            value.uv[0] += 0.125F;
            auto tx = trial->document.transaction();
            check(tx.setVertex(added, value) && tx.commit().committed,
                  "Could not tamper with interpolated UV");
            try {
                editing::validateEditGuards(source, *trial, selected, Json::object(), audit);
                throw std::runtime_error("Invalid interpolated UV accepted");
            } catch (const editing::PlanError &) {
            }
            editing::validateEditGuards(source, *trial, selected, {{"preserve_uv", false}}, audit);
            value.weights[0] = 0.25F;
            value.weights[1] = 0.75F;
            auto tx2 = trial->document.transaction();
            check(tx2.setVertex(added, value) && tx2.commit().committed,
                  "Could not tamper with interpolated weights");
            try {
                editing::validateEditGuards(source, *trial, selected, {{"preserve_uv", false}},
                                            audit);
                throw std::runtime_error("Invalid interpolated weights accepted");
            } catch (const editing::PlanError &) {
            }
            editing::validateEditGuards(source, *trial, selected,
                                        {{"preserve_uv", false}, {"preserve_weights", false}},
                                        audit);
        }
        dir = std::filesystem::temp_directory_path() /
              ("pmxer-no-clobber-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        auto temporary = dir / "temporary", destination = dir / "existing";
        {
            std::ofstream f(temporary);
            f << "new";
        }
        {
            std::ofstream f(destination);
            f << "keep";
        }
        check(!atomicCreate(temporary, destination), "Atomic create replaced existing file");
        std::string content;
        {
            std::ifstream f(destination);
            f >> content;
        }
        check(content == "keep", "Existing file content changed");
        std::filesystem::remove(destination);
        check(atomicCreate(temporary, destination), "Atomic create failed on unused path");
        {
            SaveOptions options;
            options.quietSuccess = true;
            options.preserveRecoveryFiles = true;
            options.expectedDestinationSha256 = std::string(64, '0');
            DocumentSession saving(fixture());
            const auto result = saveDocument(saving, destination, options);
            check(!result.success && result.destinationChanged,
                  "Save ignored output hash at publication");
            {
                std::ifstream f(destination);
                f >> content;
            }
            check(content == "new", "Failed precondition replaced existing output");
        }
        std::filesystem::remove_all(dir);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (!dir.empty())
            std::filesystem::remove_all(dir);
        return 1;
    }
}
