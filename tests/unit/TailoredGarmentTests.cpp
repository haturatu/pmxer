#include "editor/SaveController.hpp"
#include "editor/TailoredGarment.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
mmd::PmxModel mannequin() {
    mmd::PmxModel model;
    model.metadata.version = 2.1F;
    for (const auto &[name, p] :
         std::array<std::pair<const char *, mmd::Float3>, 9>{{{"上半身", {0, 3, 0}},
                                                              {"首", {0, 6, 0}},
                                                              {"左腕", {1, 5.5F, 0}},
                                                              {"左ひじ", {3.8F, 4, 0}},
                                                              {"右腕", {-1, 5.5F, 0}},
                                                              {"右ひじ", {-3.8F, 4, 0}},
                                                              {"上半身2", {0, 4.8F, 0}},
                                                              {"左手首", {5, 3.3F, 0}},
                                                              {"右手首", {-5, 3.3F, 0}}}}) {
        mmd::PmxBone b;
        b.name = name;
        b.position = p;
        model.bones.push_back(b);
    }
    const auto tube = [&](auto sample, int rings, int bone) {
        const auto base = static_cast<std::uint32_t>(model.vertices.size());
        constexpr std::uint32_t sides = 24;
        for (int ring = 0; ring <= rings; ++ring)
            for (std::uint32_t segment = 0; segment < sides; ++segment) {
                const auto t = static_cast<float>(ring) / static_cast<float>(rings),
                           a = static_cast<float>(segment) * 2 * std::numbers::pi_v<float> /
                               static_cast<float>(sides);
                mmd::PmxVertex v;
                v.position = sample(t, a);
                v.normal = {std::cos(a), 0, std::sin(a)};
                v.bones[0] = bone >= 0 ? bone : (v.position[1] >= 4.5F ? 6 : 0);
                model.vertices.push_back(v);
            }
        for (int ring = 0; ring < rings; ++ring)
            for (std::uint32_t segment = 0; segment < sides; ++segment) {
                const auto a = base + static_cast<std::uint32_t>(ring) * sides + segment,
                           b = base + static_cast<std::uint32_t>(ring) * sides +
                               (segment + 1) % sides;
                model.indices.insert(model.indices.end(),
                                     {a, a + sides, b + sides, a, b + sides, b});
            }
    };
    tube(
        [](float t, float a) {
            const auto w = 0.7F + 0.4F * std::sin(t * std::numbers::pi_v<float>);
            return mmd::Float3{w * std::cos(a), 3 + 3 * t, 0.5F * std::sin(a)};
        },
        8, -1);
    tube(
        [](float t, float a) {
            return mmd::Float3{(1.6F - 0.9F * t) * std::cos(a), 0.7F + 2.3F * t,
                               (0.9F - 0.4F * t) * std::sin(a)};
        },
        6, 0);
    for (float side : {-1.0F, 1.0F})
        tube(
            [&](float t, float a) {
                return mmd::Float3{side * (1 + 2.8F * t) + 0.2F * std::cos(a),
                                   5.5F - 1.5F * t + 0.1F * std::cos(a), 0.3F * std::sin(a)};
            },
            4, side > 0 ? 2 : 4);
    for (float side : {-1.0F, 1.0F})
        tube(
            [&](float t, float a) {
                return mmd::Float3{side * (3.8F + 1.2F * t) + 0.16F * std::cos(a),
                                   4 - 0.7F * t + 0.08F * std::cos(a), 0.2F * std::sin(a)};
            },
            3, side > 0 ? 3 : 5);
    mmd::PmxMaterial clothes;
    clothes.name = "Clothes";
    clothes.diffuse = {0.2F, 0.2F, 0.2F, 1};
    clothes.indexCount = static_cast<std::uint32_t>(model.indices.size());
    model.materials.push_back(clothes);
    const auto offset = static_cast<std::uint32_t>(model.vertices.size());
    for (auto p : std::array<mmd::Float3, 3>{{{-0.3F, 6.1F, 0}, {0.3F, 6.1F, 0}, {0, 6.7F, 0}}}) {
        mmd::PmxVertex v;
        v.position = p;
        v.normal = {0, 0, -1};
        v.bones[0] = 1;
        model.vertices.push_back(v);
    }
    model.indices.insert(model.indices.end(), {offset, offset + 1, offset + 2});
    auto face = clothes;
    face.name = "Face";
    face.indexCount = 3;
    model.materials.push_back(face);
    return model;
}
std::vector<std::size_t> allFaces(const pmxer::DocumentSession &s) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < s.document.faces().size(); ++i)
        result.push_back(i);
    return result;
}
} // namespace
int main() {
    try {
        using namespace pmxer;
        using namespace pmxer::ai;
        DocumentSession source(mannequin());
        const auto original = source.document.model();
        auto capture = captureFaces(source, allFaces(source));
        capture.allowMaterialExpansion = true;
        const auto target = suggestOutfitTarget(source, capture);
        require(target.materialIndices == std::vector<std::size_t>{0},
                "Local outfit scope includes the face");
        const auto region = resolveTarget(source, capture, target);
        {
            auto unnamed = mannequin();
            unnamed.materials[0].name = "Material 01";
            DocumentSession userSelection(unnamed);
            auto indices = allFaces(userSelection);
            indices.pop_back();
            auto strict = captureFaces(userSelection, indices);
            strict.allowMaterialExpansion = true;
            const auto suggested = suggestOutfitTarget(userSelection, strict);
            require(
                suggested.materialIndices == std::vector<std::size_t>{0} &&
                    !suggested.completeMaterials,
                "Explicit unnamed clothing selection was rejected or expanded beyond its scope");
        }
        Operation op;
        op.type = OperationType::generate;
        op.shape = Shape::dress;
        op.color = {0.04F, 0.03F, 0.05F, 1};
        op.garment.style = GarmentStyle::gothicLolita;
        const auto mesh = makeTailoredDress(source, region, op);
        require(mesh.materials.size() == 8 && mesh.vertices.size() > 3000,
                "Outfit is still a primitive");
        std::vector<std::size_t> counts(mesh.materials.size());
        bool interpolated = false;
        std::size_t outward = 0, skirtFaces = 0;
        for (const auto &v : mesh.vertices) {
            float weights = 0, norm = 0;
            for (auto w : v.weights) {
                require(std::isfinite(w) && w >= 0, "Invalid skin weight");
                weights += w;
            }
            for (auto c : v.position)
                require(std::isfinite(c), "Invalid generated position");
            for (auto c : v.normal)
                norm += c * c;
            require(std::abs(weights - 1) < 1e-5F && std::abs(norm - 1) < 1e-4F,
                    "Unnormalized weights or normals");
            interpolated |= v.weights[1] > 0.05F;
        }
        float bodiceExtent = 0;
        float cuffY = 0;
        float leftCuffX = 0;
        std::size_t leftCuffs = 0;
        std::size_t cuffs = 0;
        for (const auto &t : mesh.triangles) {
            require(t.material < counts.size(), "Invalid component material");
            ++counts[t.material];
            const auto a = mesh.vertices[t.a].position, b = mesh.vertices[t.b].position,
                       c = mesh.vertices[t.c].position;
            const mmd::Float3 ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
                ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const mmd::Float3 n{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                ab[0] * ac[1] - ab[1] * ac[0]};
            require(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 1e-14F,
                    "Degenerate garment triangle");
            if (t.material == 0)
                for (auto index : {t.a, t.b, t.c})
                    bodiceExtent =
                        std::max(bodiceExtent, std::abs(mesh.vertices[index].position[0]));
            if (t.material == 2) {
                ++skirtFaces;
                outward += n[0] * (a[0] + b[0] + c[0]) + n[2] * (a[2] + b[2] + c[2]) > 0 ? 1U : 0U;
            }
            if (t.material == 6) {
                if (a[0] > 0) {
                    leftCuffX += (a[0] + b[0] + c[0]) / 3;
                    ++leftCuffs;
                }
                cuffY += (a[1] + b[1] + c[1]) / 3;
                ++cuffs;
            }
        }
        for (auto n : counts)
            require(n > 0, "A declared garment part has no geometry");
        require(interpolated, "Skin weights still snap to individual source vertices");
        require(bodiceExtent < 1.6F, "Bodice was fitted to the entire arm span");
        require(outward * 100 > skirtFaces * 95, "Skirt normals point inward");
        require(cuffY / static_cast<float>(cuffs) < 5.1F, "Sleeves ignore the original A-pose");
        require(leftCuffs > 0 && std::abs(leftCuffX / static_cast<float>(leftCuffs) - 5) < 0.25F,
                "Default sleeves expose missing arms instead of reaching the original wrists");
        auto noSleeves = op;
        noSleeves.garment.sleeveLength = 0;
        const auto sleeveless = makeTailoredDress(source, region, noSleeves);
        for (const auto &face : sleeveless.triangles)
            require(face.material != 1 && face.material != 6,
                    "Sleeveless option retained sleeve geometry");
        auto fewer = op;
        fewer.garment.tiers = 1;
        fewer.garment.lace = false;
        fewer.garment.bowSize = 0;
        const auto simple = makeTailoredDress(source, region, fewer);
        require(simple.triangles.size() < mesh.triangles.size() && simple.materials.size() == 7,
                "Garment parameters do not change construction");
        const auto protectedFace = source.document.faceHandle(source.document.faces().size() - 1);
        const auto beforeFace = *source.document.resolve(protectedFace);
        const EditPlan plan{"Gothic outfit", {op}, target};
        const auto preview = makeEditPreview(source, capture, plan);
        require(mmd::pmx::semanticCompare(original, source.document.model()).equal(),
                "Preview modified the original");
        require(acceptEditPreview(source, capture, *preview).success &&
                    *source.document.resolve(protectedFace) == beforeFace,
                "Outfit changed the face");
        const auto edited = source.document.model();
        auto recaptured = captureFaces(source, allFaces(source));
        recaptured.allowMaterialExpansion = true;
        require(suggestOutfitTarget(source, recaptured).materialIndices.size() == 8,
                "Editing a generated outfit again selected only its skirt");
        require(source.commands.undoCount() == 1 && source.undo() &&
                    mmd::pmx::semanticCompare(original, source.document.model()).equal() &&
                    source.redo(),
                "Outfit undo/redo failed");
        const auto path = std::filesystem::temp_directory_path() /
                          ("pmxer-tailored-" + source.recoveryId + ".pmx");
        require(saveDocument(source, path).success &&
                    mmd::pmx::semanticCompare(edited, mmd::pmx::load(path)).equal(),
                "Tailored outfit did not round-trip");
        std::filesystem::remove(path);
        require(source.undo(), "Outfit reset failed");
        const auto fresh = captureFaces(source, allFaces(source));
        auto invalid = plan;
        invalid.operations[0].garment.fullness = std::numeric_limits<float>::quiet_NaN();
        require(!applyEditPlan(source, fresh, invalid).success &&
                    mmd::pmx::semanticCompare(original, source.document.model()).equal(),
                "Invalid garment changed the model");
        nlohmann::json garment{{"style", "gothic_lolita"},
                               {"trimColor", {1, 1, 1, 1}},
                               {"accentColor", {0.3, 0, 0, 1}},
                               {"tiers", 3},
                               {"skirtLength", 1},
                               {"sleeveLength", 2},
                               {"fullness", 1},
                               {"bowSize", 1},
                               {"lace", true}};
        nlohmann::json json{{"version", 1},
                            {"summary", "Gothic outfit"},
                            {"operations", nlohmann::json::array({{{"type", "generate"},
                                                                   {"shape", "dress"},
                                                                   {"color", {0.04, 0.03, 0.05, 1}},
                                                                   {"translation", {0, 0, 0}},
                                                                   {"scale", {1, 1, 1}},
                                                                   {"rotation", {0, 0, 0}},
                                                                   {"replace", true},
                                                                   {"detail", 24},
                                                                   {"garment", garment}}})}};
        require(parsePlan(json.dump()).operations[0].garment.style == GarmentStyle::gothicLolita,
                "Structured Gothic outfit ignored its style");
        const auto rejected = [&](const nlohmann::json &bad) {
            try {
                (void)parsePlan(bad.dump());
            } catch (const std::exception &) {
                return;
            }
            throw std::runtime_error("Malformed garment plan accepted");
        };
        auto bad = json;
        bad["operations"][0]["garment"]["tiers"] = 4294967299LL;
        rejected(bad);
        bad = json;
        bad["operations"][0]["garment"]["style"] = "mesh_service";
        rejected(bad);
        bad = json;
        bad["operations"][0]["garment"]["fullness"] = 0;
        rejected(bad);
        bad = json;
        bad["operations"][0]["garment"]["sleeveLength"] = 5;
        rejected(bad);
        bad = json;
        bad["operations"][0]["garment"]["textureUrl"] = "https://example.invalid";
        rejected(bad);
        bad = json;
        bad["operations"][0]["shape"] = "pants";
        rejected(bad);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR %s\n", error.what());
        return 1;
    }
}
