#include "AiEditExecutor.hpp"
#include "i18n/Language.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace pmxer::ai {
namespace {
using V = mmd::Float3;
V sub(V a, V b) {
    for (std::size_t i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float squared(V a) { return a[0] * a[0] + a[1] * a[1] + a[2] * a[2]; }
V normalized(V a) {
    const auto length = std::sqrt(squared(a));
    if (length < 1e-8F)
        return {0, 1, 0};
    for (auto &v : a)
        v /= length;
    return a;
}
V rotate(V p, V degrees) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
        const auto angle = degrees[axis] * std::numbers::pi_v<float> / 180.0F;
        const auto c = std::cos(angle), s = std::sin(angle), first = p[a], second = p[b];
        p[a] = c * first - s * second;
        p[b] = s * first + c * second;
    }
    return p;
}
V center(const EditRegion &region) {
    V result{};
    for (std::size_t i = 0; i < 3; ++i)
        result[i] = (region.minimum[i] + region.maximum[i]) * 0.5F;
    return result;
}
std::string namePrefix(const std::string &text) {
    auto length = std::min<std::size_t>(256, text.size());
    while (length < text.size() && length > 0 &&
           (static_cast<unsigned char>(text[length]) & 0xc0U) == 0x80U)
        --length;
    return text.substr(0, length);
}
class AiSnapshotCommand final : public EditorCommand {
  public:
    AiSnapshotCommand(std::shared_ptr<mmd::PmxDocument> before,
                      std::shared_ptr<mmd::PmxDocument> after)
        : before_(std::move(before)), after_(std::move(after)),
          description_(i18n::tr("AI region edit")) {}
    bool apply(mmd::PmxDocument &document) override {
        document.restoreSnapshot(*after_, document.domain());
        return true;
    }
    bool undo(mmd::PmxDocument &document) override {
        document.restoreSnapshot(*before_, document.domain());
        return true;
    }
    const std::string &description() const noexcept override { return description_; }

  private:
    std::shared_ptr<mmd::PmxDocument> before_, after_;
    std::string description_;
};

// Face replacement can reorder tens of thousands of faces. Stable-handle snapshots
// avoid quadratic patch-order replay while preserving the original document domain.
OperationResult
commitAiTransaction(DocumentSession &session,
                    const std::function<bool(mmd::PmxDocument::Transaction &)> &edit) {
    auto before = std::make_shared<mmd::PmxDocument>();
    before->restoreSnapshot(session.document, session.document.domain());
    auto transaction = session.document.transaction();
    if (!edit(transaction))
        return {false, "AI edit target could not be resolved"};
    auto committed = transaction.commit();
    if (!committed.committed)
        return {false,
                committed.errors.empty() ? "AI edit failed validation" : committed.errors.front()};
    if (committed.patch.empty())
        return {true, {}};
    auto after = std::make_shared<mmd::PmxDocument>();
    after->restoreSnapshot(session.document, session.document.domain());
    session.commands.recordApplied(
        std::make_unique<AiSnapshotCommand>(std::move(before), std::move(after)));
    session.modified = session.commands.isModified();
    ++session.revision;
    session.validation = std::move(committed.validation);
    session.changes = std::move(committed.changes);
    return {true, {}};
}

struct Mesh {
    std::vector<mmd::PmxVertex> vertices;
    struct Triangle {
        std::size_t a, b, c;
        bool sole{};
    };
    std::vector<Triangle> triangles;
    std::size_t vertex(V p, mmd::Float2 uv = {}) {
        mmd::PmxVertex v;
        v.position = p;
        v.uv = uv;
        vertices.push_back(v);
        return vertices.size() - 1;
    }
    void quad(std::size_t a, std::size_t b, std::size_t c, std::size_t d, bool sole = false) {
        triangles.push_back({a, b, c, sole});
        triangles.push_back({a, c, d, sole});
    }
    void box(V lo, V hi, bool sole = false) {
        const auto base = vertices.size();
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x)
                    vertex({x ? hi[0] : lo[0], y ? hi[1] : lo[1], z ? hi[2] : lo[2]});
        for (const auto &q : std::array<std::array<std::size_t, 4>, 6>{{{0, 2, 3, 1},
                                                                        {4, 5, 7, 6},
                                                                        {0, 1, 5, 4},
                                                                        {2, 6, 7, 3},
                                                                        {0, 4, 6, 2},
                                                                        {1, 3, 7, 5}}})
            quad(base + q[0], base + q[1], base + q[2], base + q[3], sole);
    }
};

void shoe(Mesh &mesh, V lo, V hi, int detail) {
    const auto width = std::max(hi[0] - lo[0], 0.01F), depth = std::max(hi[2] - lo[2], 0.01F);
    const auto height = std::max(0.01F, std::min(hi[1] - lo[1], depth * 0.28F));
    const auto cx = (lo[0] + hi[0]) * 0.5F, cz = (lo[2] + hi[2]) * 0.5F;
    const auto base = mesh.vertices.size();
    // Outsole, welt, vamp, and the open ankle rim. The toe faces -Z (MMD front).
    constexpr std::array levels{0.0F, 0.10F, 0.22F, 0.55F, 0.85F};
    for (std::size_t ring = 0; ring < levels.size(); ++ring) {
        for (int segment = 0; segment < detail; ++segment) {
            const auto t = static_cast<float>(segment) / static_cast<float>(detail);
            const auto angle = t * 2 * std::numbers::pi_v<float>;
            const bool opening = ring == levels.size() - 1;
            const auto zRadius = opening ? 0.20F : 0.5F;
            const auto xRadius = opening ? 0.33F : 0.5F;
            const auto z = std::sin(angle);
            const auto heelTaper = opening ? 1.0F : (z > 0 ? 0.82F : 1.0F);
            mesh.vertex({cx + std::cos(angle) * width * xRadius * heelTaper,
                         lo[1] + levels[ring] * height,
                         cz + (opening ? 0.23F * depth : 0.0F) + z * depth * zRadius},
                        {t, levels[ring]});
        }
    }
    const auto n = static_cast<std::size_t>(detail);
    for (std::size_t ring = 0; ring + 1 < levels.size(); ++ring)
        for (std::size_t s = 0; s < n; ++s)
            mesh.quad(base + ring * n + s, base + ring * n + (s + 1) % n,
                      base + (ring + 1) * n + (s + 1) % n, base + (ring + 1) * n + s, ring == 0);
    const auto bottom = mesh.vertex({cx, lo[1], cz});
    for (std::size_t s = 0; s < n; ++s)
        mesh.triangles.push_back({bottom, base + (s + 1) % n, base + s, true});
    // Penny strap and heel are separate, intersecting solids, like sewn shoe parts.
    mesh.box({cx - width * 0.44F, lo[1] + height * 0.55F, cz - depth * 0.12F},
             {cx + width * 0.44F, lo[1] + height * 0.60F, cz - depth * 0.02F});
    mesh.box({cx - width * 0.35F, lo[1] - height * 0.05F, cz + depth * 0.13F},
             {cx + width * 0.35F, lo[1] + height * 0.10F, hi[2] - depth * 0.05F}, true);
}

void garment(Mesh &mesh, const EditRegion &region, const Operation &op) {
    const auto c = center(region);
    const auto w = std::max(region.maximum[0] - region.minimum[0], 0.01F);
    const auto h = std::max(region.maximum[1] - region.minimum[1], 0.01F);
    const auto d = std::max(region.maximum[2] - region.minimum[2], 0.01F);
    const auto n = static_cast<std::size_t>(op.detail);
    const auto base = mesh.vertices.size();
    constexpr std::size_t rings = 7;
    const bool skirt = op.shape == Shape::skirt;
    const auto torso = (!skirt && w > h * 1.3F) ? 0.52F : 1.0F;
    for (std::size_t ring = 0; ring < rings; ++ring) {
        const auto t = static_cast<float>(ring) / static_cast<float>(rings - 1);
        const auto radius =
            skirt ? (0.52F - 0.17F * t)
                  : (ring == rings - 1 ? 0.18F
                                       : 0.47F - 0.035F * std::sin(t * std::numbers::pi_v<float>));
        for (std::size_t s = 0; s < n; ++s) {
            const auto u = static_cast<float>(s) / static_cast<float>(n);
            const auto a = u * 2 * std::numbers::pi_v<float>;
            const auto pleat =
                skirt ? 1.0F + 0.055F * (1 - t) * std::cos(a * static_cast<float>(n / 2)) : 1.0F;
            mesh.vertex({c[0] + std::cos(a) * w * torso * radius * pleat * 1.03F,
                         region.minimum[1] + t * h,
                         c[2] + std::sin(a) * d * radius * pleat * 1.05F},
                        {u, t});
        }
    }
    for (std::size_t ring = 0; ring + 1 < rings; ++ring)
        for (std::size_t s = 0; s < n; ++s)
            mesh.quad(base + ring * n + s, base + ring * n + (s + 1) % n,
                      base + (ring + 1) * n + (s + 1) % n, base + (ring + 1) * n + s);
    if (skirt)
        return;
    // Open sleeves extend from the shoulder to each side of the captured region.
    for (float side : {-1.0F, 1.0F}) {
        const auto sleeve = mesh.vertices.size();
        for (int ring = 0; ring < 3; ++ring) {
            const auto t = static_cast<float>(ring) * 0.5F;
            for (std::size_t s = 0; s < n; ++s) {
                const auto u = static_cast<float>(s) / static_cast<float>(n),
                           a = u * 2 * std::numbers::pi_v<float>;
                mesh.vertex({c[0] + side * w * (torso * 0.38F + (0.50F - torso * 0.38F) * t),
                             region.minimum[1] + h * (0.80F - 0.055F * t) +
                                 std::cos(a) * h * (0.13F - 0.025F * t),
                             c[2] + std::sin(a) * d * (0.34F - 0.04F * t)},
                            {u, t});
            }
        }
        for (std::size_t ring = 0; ring < 2; ++ring)
            for (std::size_t s = 0; s < n; ++s) {
                if (side > 0)
                    mesh.quad(sleeve + ring * n + s, sleeve + (ring + 1) * n + s,
                              sleeve + (ring + 1) * n + (s + 1) % n,
                              sleeve + ring * n + (s + 1) % n);
                else
                    mesh.quad(sleeve + ring * n + s, sleeve + ring * n + (s + 1) % n,
                              sleeve + (ring + 1) * n + (s + 1) % n, sleeve + (ring + 1) * n + s);
            }
    }
}

Mesh generate(const DocumentSession &session, const EditRegion &region, const Operation &op) {
    Mesh mesh;
    if (op.shape == Shape::loafers) {
        std::array<std::vector<V>, 2> groups;
        const bool pair =
            region.minimum[0] < -0.01F && region.maximum[0] > 0.01F &&
            region.maximum[0] - region.minimum[0] > (region.maximum[2] - region.minimum[2]) * 0.8F;
        for (auto handle : region.vertices) {
            const auto p = session.document.resolve(handle)->position;
            groups[pair && p[0] > 0 ? 1U : 0U].push_back(p);
        }
        for (const auto &group : groups) {
            if (group.empty())
                continue;
            V lo = group.front(), hi = lo;
            for (const auto &p : group)
                for (std::size_t i = 0; i < 3; ++i) {
                    lo[i] = std::min(lo[i], p[i]);
                    hi[i] = std::max(hi[i], p[i]);
                }
            shoe(mesh, lo, hi, op.detail);
        }
    } else
        garment(mesh, region, op);
    const auto pivot = center(region);
    for (auto &v : mesh.vertices) {
        auto p = sub(v.position, pivot);
        for (std::size_t i = 0; i < 3; ++i)
            p[i] *= op.scale[i];
        p = rotate(p, op.rotation);
        for (std::size_t i = 0; i < 3; ++i)
            v.position[i] = p[i] + pivot[i] + op.translation[i];
        float distance = std::numeric_limits<float>::max();
        const mmd::PmxVertex *nearest = nullptr;
        for (auto handle : region.vertices) {
            const auto *source = session.document.resolve(handle);
            const auto candidate = squared(sub(v.position, source->position));
            if (candidate < distance) {
                distance = candidate;
                nearest = source;
            }
        }
        if (nearest) {
            v.bones = nearest->bones;
            v.weights = nearest->weights;
            v.weightType = nearest->weightType == mmd::PmxWeightType::sdef
                               ? mmd::PmxWeightType::bdef2
                               : nearest->weightType;
        }
        v.normal = {};
    }
    for (auto &face : mesh.triangles)
        std::swap(face.b, face.c);
    for (const auto &face : mesh.triangles) {
        const auto normal =
            cross(sub(mesh.vertices[face.b].position, mesh.vertices[face.a].position),
                  sub(mesh.vertices[face.c].position, mesh.vertices[face.a].position));
        for (auto index : {face.a, face.b, face.c})
            for (std::size_t i = 0; i < 3; ++i)
                mesh.vertices[index].normal[i] += normal[i];
    }
    for (auto &v : mesh.vertices)
        v.normal = normalized(v.normal);
    return mesh;
}
} // namespace

EditRegion captureFaces(const DocumentSession &session, std::span<const std::size_t> indices) {
    EditRegion result;
    result.documentId = session.recoveryId;
    result.domain = session.document.domain();
    result.revision = session.revision;
    result.minimum.fill(std::numeric_limits<float>::max());
    result.maximum.fill(std::numeric_limits<float>::lowest());
    std::set<std::uint64_t> vertices;
    std::set<std::size_t> unique;
    std::vector<std::size_t> sorted(indices.begin(), indices.end());
    std::sort(sorted.begin(), sorted.end());
    for (auto index : sorted) {
        if (index >= session.document.faces().size() || !unique.insert(index).second)
            throw std::runtime_error("Invalid AI face selection");
        auto handle = session.document.faceHandle(index);
        result.faces.push_back(handle);
        result.faceIndices.push_back(index);
        for (auto vertex : session.document.resolve(handle)->vertices) {
            const auto *value = session.document.resolve(vertex);
            if (!value)
                throw std::runtime_error("Stale AI vertex selection");
            for (std::size_t i = 0; i < 3; ++i) {
                result.minimum[i] = std::min(result.minimum[i], value->position[i]);
                result.maximum[i] = std::max(result.maximum[i], value->position[i]);
            }
            if (vertices.insert(vertex.id).second)
                result.vertices.push_back(vertex);
        }
    }
    if (result.faces.empty())
        throw std::runtime_error("Select faces, vertices, or a material first");
    return result;
}

EditRegion captureRegion(const DocumentSession &session) {
    std::set<std::uint64_t> faces, materials, vertices;
    for (const auto &item : session.selection.items()) {
        if (item.domain != session.document.domain())
            throw std::runtime_error("Stale AI selection");
        if (item.kind == SelectionKind::face) {
            const auto h = selectionHandle<mmd::FaceTag>(session.document, item);
            if (!session.document.resolve(h))
                throw std::runtime_error("Stale AI selection");
            faces.insert(item.id);
        } else if (item.kind == SelectionKind::material) {
            if (!session.document.resolve(
                    selectionHandle<mmd::MaterialTag>(session.document, item)))
                throw std::runtime_error("Stale AI selection");
            materials.insert(item.id);
        } else if (item.kind == SelectionKind::vertex) {
            if (!session.document.resolve(selectionHandle<mmd::VertexTag>(session.document, item)))
                throw std::runtime_error("Stale AI selection");
            vertices.insert(item.id);
        }
    }
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < session.document.faces().size(); ++i) {
        const auto &face = session.document.faces()[i];
        if (faces.contains(session.document.faceHandle(i).id) ||
            materials.contains(face.material.id) ||
            (vertices.contains(face.vertices[0].id) && vertices.contains(face.vertices[1].id) &&
             vertices.contains(face.vertices[2].id)))
            indices.push_back(i);
    }
    return captureFaces(session, indices);
}

nlohmann::json regionContext(const DocumentSession &session, const EditRegion &region) {
    nlohmann::json materials = nlohmann::json::array(), bones = nlohmann::json::array();
    std::set<std::uint64_t> seenMaterials;
    std::set<std::int32_t> seenBones;
    for (auto face : region.faces) {
        const auto *value = session.document.resolve(face);
        if (!value)
            throw std::runtime_error("Stale AI selection");
        if (seenMaterials.insert(value->material.id).second && materials.size() < 64) {
            const auto *material = session.document.resolve(value->material);
            materials.push_back({{"name", namePrefix(material->name)},
                                 {"englishName", namePrefix(material->englishName)},
                                 {"color", material->diffuse}});
        }
    }
    for (auto vertex : region.vertices)
        for (auto bone : session.document.resolve(vertex)->bones)
            if (bone >= 0 &&
                static_cast<std::size_t>(bone) < session.document.model().bones.size() &&
                seenBones.insert(bone).second && bones.size() < 64) {
                const auto &b = session.document.model().bones[static_cast<std::size_t>(bone)];
                bones.push_back(
                    {{"name", namePrefix(b.name)}, {"englishName", namePrefix(b.englishName)}});
            }
    return {{"selectedFaces", region.faces.size()},
            {"selectedVertices", region.vertices.size()},
            {"boundsMin", region.minimum},
            {"boundsMax", region.maximum},
            {"materials", materials},
            {"influencingBones", bones}};
}

OperationResult applyEditPlan(DocumentSession &session, const EditRegion &region,
                              const EditPlan &plan) {
    if (region.domain != session.document.domain() || region.documentId != session.recoveryId ||
        region.revision != session.revision)
        return {false, i18n::tr("The model changed. Capture the selection again.")};
    if (session.hasPendingTransformEdit())
        return {false, i18n::tr("Finish or discard temporary transforms first.")};
    if (plan.operations.empty() || plan.operations.size() > 8 || region.faces.empty())
        return {false, i18n::tr("No supported edits were generated.")};
    for (auto h : region.faces)
        if (!session.document.resolve(h))
            return {false, i18n::tr("The model changed. Capture the selection again.")};
    for (auto h : region.vertices)
        if (!session.document.resolve(h))
            return {false, i18n::tr("The model changed. Capture the selection again.")};
    const auto extent = std::max(0.01F, std::sqrt(squared(sub(region.maximum, region.minimum))));
    for (const auto &op : plan.operations) {
        for (std::size_t i = 0; i < 3; ++i)
            if (!std::isfinite(op.scale[i]) || op.scale[i] < 0.25F || op.scale[i] > 4 ||
                !std::isfinite(op.translation[i]) || std::abs(op.translation[i]) > extent * 2 ||
                !std::isfinite(op.rotation[i]) || std::abs(op.rotation[i]) > 180)
                return {false, "AI transform is out of range"};
        for (auto value : op.color)
            if (!std::isfinite(value) || value < 0 || value > 1)
                return {false, "AI color is out of range"};
        if (op.detail < 8 || op.detail > 64 ||
            (op.type == OperationType::generate &&
             (op.shape == Shape::none || plan.operations.size() != 1)))
            return {false, "Invalid AI generation parameters"};
    }
    const bool generates = plan.operations.front().type == OperationType::generate;
    const bool transforms =
        std::any_of(plan.operations.begin(), plan.operations.end(),
                    [](const auto &op) { return op.type == OperationType::transform; });
    const auto mesh = generates ? generate(session, region, plan.operations.front()) : Mesh{};
    const auto originalMaterialCount = session.document.model().materials.size();
    const auto pivot = center(region);
    const auto originalFaces = session.document.faces();
    const std::set<mmd::FaceHandle> chosen(region.faces.begin(), region.faces.end());
    std::set<std::uint64_t> outsideVertices;
    for (std::size_t i = 0; i < originalFaces.size(); ++i)
        if (!chosen.contains(session.document.faceHandle(i)))
            for (auto v : originalFaces[i].vertices)
                outsideVertices.insert(v.id);
    auto result = commitAiTransaction(session, [&](auto &tx) {
        if (generates) {
            const auto &op = plan.operations.front();
            mmd::PmxMaterial material;
            material.name = op.shape == Shape::loafers ? "AI Loafers"
                            : op.shape == Shape::shirt ? "AI Shirt"
                                                       : "AI Skirt";
            material.englishName = material.name;
            material.diffuse = op.color;
            material.drawFlags = 1;
            material.ambient = {0.18F, 0.18F, 0.18F};
            material.specular = {0.12F, 0.12F, 0.12F};
            material.shininess = 24;
            const auto main = tx.addMaterial(material);
            auto sole = main;
            if (op.shape == Shape::loafers) {
                material.name = "AI Sole";
                material.englishName = material.name;
                material.diffuse = {0.025F, 0.025F, 0.025F, 1};
                sole = tx.addMaterial(material);
            }
            std::vector<mmd::VertexHandle> added;
            for (const auto &v : mesh.vertices)
                added.push_back(tx.addVertex(v));
            for (const auto &f : mesh.triangles)
                (void)tx.addFace(added[f.a], added[f.b], added[f.c], f.sole ? sole : main);
            if (op.replace)
                for (auto it = region.faces.rbegin(); it != region.faces.rend(); ++it)
                    if (!tx.eraseFace(*it))
                        return false;
            return true;
        }
        std::map<std::uint64_t, mmd::VertexHandle> moved;
        if (transforms)
            for (auto h : region.vertices) {
                auto vertex = *session.document.resolve(h);
                for (const auto &op : plan.operations)
                    if (op.type == OperationType::transform) {
                        auto p = sub(vertex.position, pivot);
                        for (std::size_t i = 0; i < 3; ++i) {
                            p[i] *= op.scale[i];
                            vertex.normal[i] /= op.scale[i];
                        }
                        p = rotate(p, op.rotation);
                        vertex.normal = normalized(rotate(vertex.normal, op.rotation));
                        for (std::size_t i = 0; i < 3; ++i)
                            vertex.position[i] = p[i] + pivot[i] + op.translation[i];
                    }
                if (outsideVertices.contains(h.id)) {
                    const auto replacement = tx.addVertex(vertex);
                    moved[h.id] = replacement;
                    // Preserve vertex/UV morph deformation on split seam vertices.
                    for (std::size_t m = 0; m < session.document.model().morphs.size(); ++m) {
                        const auto &morph = session.document.model().morphs[m];
                        if (morph.type != 1 && (morph.type < 3 || morph.type > 7))
                            continue;
                        for (const auto &offset : morph.offsets) {
                            if (offset.index < 0 ||
                                static_cast<std::size_t>(offset.index) >=
                                    session.document.model().vertices.size() ||
                                session.document.vertexHandle(
                                    static_cast<std::size_t>(offset.index)) != h)
                                continue;
                            if (morph.type == 1 &&
                                !tx.addVertexMorphOffset(session.document.morphHandle(m),
                                                         replacement, offset.vector3))
                                return false;
                            if (morph.type >= 3 && morph.type <= 7 &&
                                !tx.addUvMorphOffset(session.document.morphHandle(m), replacement,
                                                     morph.type - 3U, offset.vector4))
                                return false;
                        }
                    }
                } else if (!tx.setVertex(h, vertex))
                    return false;
            }
        std::map<std::uint64_t, mmd::MaterialHandle> recolored;
        const Operation *color = nullptr;
        for (const auto &op : plan.operations)
            if (op.type == OperationType::recolor)
                color = &op;
        for (auto it = region.faces.rbegin(); it != region.faces.rend(); ++it) {
            const auto h = *it;
            const auto &face = *session.document.resolve(h);
            auto material = face.material;
            if (color) {
                if (!recolored.contains(material.id)) {
                    auto copy = *session.document.resolve(material);
                    copy.name = "AI " + copy.name;
                    copy.englishName = "AI " + copy.englishName;
                    copy.diffuse = color->color;
                    copy.textureIndex = -1;
                    copy.sphereTextureIndex = -1;
                    copy.sphereMode = 0;
                    recolored[material.id] = tx.addMaterial(copy);
                }
                material = recolored.at(material.id);
            }
            bool split = false;
            for (auto v : face.vertices)
                split |= moved.contains(v.id);
            if (split) {
                const auto get = [&](auto v) { return moved.contains(v.id) ? moved.at(v.id) : v; };
                if (!tx.eraseFace(h))
                    return false;
                (void)tx.addFace(get(face.vertices[0]), get(face.vertices[1]),
                                 get(face.vertices[2]), material);
            } else if (color && !tx.setFaceMaterial(h, material))
                return false;
        }
        return true;
    });
    if (result.success) {
        if (!session.ui.isolatedMaterials.empty())
            for (auto index = originalMaterialCount;
                 index < session.document.model().materials.size(); ++index) {
                const auto h = session.document.materialHandle(index);
                session.ui.isolatedMaterials.push_back(
                    {SelectionKind::material, h.domain, h.id, h.generation});
            }
        session.selection.retainAlive(session.document);
        session.ui.clearDrafts();
        session.ui.viewportPickCache.clear();
    }
    return result;
}

std::unique_ptr<DocumentSession> makeEditPreview(const DocumentSession &source,
                                                 const EditRegion &captured, const EditPlan &plan) {
    if (captured.documentId != source.recoveryId || captured.domain != source.document.domain() ||
        captured.revision != source.revision)
        throw std::runtime_error(i18n::tr("The model changed. Capture the selection again."));
    if (source.hasPendingTransformEdit())
        throw std::runtime_error(i18n::tr("Finish or discard temporary transforms first."));
    auto preview = std::make_unique<DocumentSession>();
    const auto domain = preview->document.domain();
    preview->document.restoreSnapshot(source.document, domain);
    preview->revision = source.revision;
    preview->ui = source.ui;
    // Never borrow a frame whose vertex count belongs to the original topology.
    preview->ui.previewFrame = nullptr;
    preview->ui.viewportPickCache.clear();
    preview->ui.viewportHover.reset();
    preview->ui.gizmoDragging = false;
    preview->ui.boxSelecting = false;
    preview->ui.morphOffsetTarget = {};
    preview->ui.aiPreviewReadOnly = true;
    preview->ui.aiRegionSelection = false;
    // Snapshot handles retain IDs and generations, with an independent domain.
    for (auto *items : {&preview->ui.hiddenMaterials, &preview->ui.isolatedMaterials})
        for (auto &item : *items)
            item.domain = domain;
    preview->preview.motion = source.preview.motion;
    preview->preview.pose = source.preview.pose;
    preview->preview.morphValues = source.preview.morphValues;
    for (auto &blend : preview->preview.morphValues)
        blend.morph.domain = domain;
    preview->preview.solo = source.preview.solo;
    preview->preview.soloMorph = source.preview.soloMorph;
    preview->preview.soloMorph.domain = domain;
    preview->previewIk = source.previewIk;
    preview->previewPhysics = false;
    const auto region = captureFaces(*preview, captured.faceIndices);
    const auto result = applyEditPlan(*preview, region, plan);
    if (!result.success)
        throw std::runtime_error(result.message);
    return preview;
}
} // namespace pmxer::ai
