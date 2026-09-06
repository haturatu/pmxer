#include "ModelMergeTool.hpp"

#include "../EditorOperations.hpp"

#include <algorithm>

namespace pmxer {
namespace {

template <typename Value>
void collectConflicts(std::vector<MergeConflict> &conflicts, const std::vector<Value> &left, const std::vector<Value> &right,
                      const char *kind) {
    for (const auto &a : left)
        for (const auto &b : right)
            if (!a.name.empty() && a.name == b.name)
                conflicts.push_back({kind, a.name, a.name, b.name});
}

} // namespace

MergeReport inspectMerge(const mmd::PmxModel &left, const mmd::PmxModel &right) {
    MergeReport report;
    collectConflicts(report.conflicts, left.materials, right.materials, "material");
    collectConflicts(report.conflicts, left.bones, right.bones, "bone");
    collectConflicts(report.conflicts, left.morphs, right.morphs, "morph");
    return report;
}

bool mergeAppend(DocumentSession &session, const mmd::PmxModel &other, MergeReport *report) {
    MergeReport local = inspectMerge(session.document.model(), other);
    if (!mmd::pmx::validate(other).valid())
        return false;
    const auto &base = session.document.model();
    const auto vertexBase = base.vertices.size();
    const auto textureBase = base.textures.size();
    const auto materialBase = base.materials.size();
    const auto boneBase = base.bones.size();
    const auto morphBase = base.morphs.size();
    const auto rigidBodyBase = base.rigidBodies.size();
    std::vector<mmd::MaterialHandle> newMaterials;
    std::vector<mmd::VertexHandle> newVertices;
    const auto result = applyTransaction(session, [&](auto &transaction) {
        for (const auto &texture : other.textures)
            if (!transaction.addTexture(texture))
                return false;
        for (const auto &material : other.materials) {
            auto value = material;
            value.textureIndex += value.textureIndex < 0 ? 0 : static_cast<std::int32_t>(textureBase);
            value.sphereTextureIndex += value.sphereTextureIndex < 0 ? 0 : static_cast<std::int32_t>(textureBase);
            if (value.toonMode == 0)
                value.toonTextureIndex += value.toonTextureIndex < 0 ? 0 : static_cast<std::int32_t>(textureBase);
            const auto handle = transaction.addMaterial(std::move(value));
            if (!handle)
                return false;
            newMaterials.push_back(handle);
        }
        for (const auto &bone : other.bones) {
            auto value = bone;
            const auto remap = [boneBase](std::int32_t index) {
                return index < 0 ? index : index + static_cast<std::int32_t>(boneBase);
            };
            value.parent = remap(value.parent);
            value.tailBone = remap(value.tailBone);
            value.inheritParent = remap(value.inheritParent);
            value.ikTarget = remap(value.ikTarget);
            for (auto &link : value.ikLinks)
                link.bone = remap(link.bone);
            if (!transaction.addBone(std::move(value)))
                return false;
        }
        for (const auto &vertex : other.vertices)
        {
            auto value = vertex;
            const auto count = value.weightType == mmd::PmxWeightType::bdef1
                                   ? std::size_t{1}
                                   : (value.weightType == mmd::PmxWeightType::bdef2 ||
                                              value.weightType == mmd::PmxWeightType::sdef
                                          ? std::size_t{2}
                                          : std::size_t{4});
            for (std::size_t i = 0; i < count; ++i)
                if (value.bones[i] >= 0)
                    value.bones[i] += static_cast<std::int32_t>(boneBase);
            const auto handle = transaction.addVertex(std::move(value));
            if (!handle)
                return false;
            newVertices.push_back(handle);
        }
        std::size_t indexOffset{};
        for (std::size_t material = 0; material < other.materials.size(); ++material) {
            const auto count = other.materials[material].indexCount;
            const auto end = indexOffset + count;
            const auto materialHandle = newMaterials[material];
            if (!materialHandle)
                return false;
            for (std::size_t index = indexOffset; index + 2 < end; index += 3) {
                if (index + 2 >= other.indices.size())
                    return false;
                const auto a = other.indices[index] + static_cast<std::uint32_t>(vertexBase);
                const auto b = other.indices[index + 1] + static_cast<std::uint32_t>(vertexBase);
                const auto c = other.indices[index + 2] + static_cast<std::uint32_t>(vertexBase);
                if (a < vertexBase || b < vertexBase || c < vertexBase ||
                    static_cast<std::size_t>(a - vertexBase) >= newVertices.size() ||
                    static_cast<std::size_t>(b - vertexBase) >= newVertices.size() ||
                    static_cast<std::size_t>(c - vertexBase) >= newVertices.size() ||
                    !transaction.addFace(newVertices[static_cast<std::size_t>(a - vertexBase)],
                                         newVertices[static_cast<std::size_t>(b - vertexBase)],
                                         newVertices[static_cast<std::size_t>(c - vertexBase)], materialHandle))
                    return false;
            }
            indexOffset = end;
        }
        for (const auto &morph : other.morphs) {
            auto value = morph;
            for (auto &offset : value.offsets) {
                if (value.type == 0 || value.type == 9)
                    offset.index += offset.index < 0 ? 0 : static_cast<std::int32_t>(morphBase);
                else if (value.type == 1 || (value.type >= 3 && value.type <= 7))
                    offset.index += offset.index < 0 ? 0 : static_cast<std::int32_t>(vertexBase);
                else if (value.type == 2)
                    offset.index += offset.index < 0 ? 0 : static_cast<std::int32_t>(boneBase);
                else if (value.type == 8 && offset.index >= 0)
                    offset.index += static_cast<std::int32_t>(materialBase);
                else if (value.type == 10)
                    offset.index += offset.index < 0 ? 0 : static_cast<std::int32_t>(rigidBodyBase);
            }
            if (!transaction.addMorph(std::move(value)))
                return false;
        }
        for (const auto &body : other.rigidBodies) {
            auto value = body;
            if (value.bone >= 0)
                value.bone += static_cast<std::int32_t>(boneBase);
            if (!transaction.addRigidBody(std::move(value)))
                return false;
        }
        for (const auto &joint : other.joints) {
            auto value = joint;
            if (value.bodyA >= 0)
                value.bodyA += static_cast<std::int32_t>(rigidBodyBase);
            if (value.bodyB >= 0)
                value.bodyB += static_cast<std::int32_t>(rigidBodyBase);
            if (!transaction.addJoint(std::move(value)))
                return false;
        }
        for (const auto &frame : other.displayFrames) {
            auto value = frame;
            for (auto &item : value.items)
                item.index += item.index < 0 ? 0 : static_cast<std::int32_t>(item.bone ? boneBase : morphBase);
            if (!transaction.addDisplayFrame(std::move(value)))
                return false;
        }
        for (const auto &soft : other.softBodies) {
            auto value = soft;
            if (value.material >= 0)
                value.material += static_cast<std::int32_t>(materialBase);
            for (auto &anchor : value.anchors) {
                if (anchor.rigidBody >= 0)
                    anchor.rigidBody += static_cast<std::int32_t>(rigidBodyBase);
                if (anchor.vertex >= 0)
                    anchor.vertex += static_cast<std::int32_t>(vertexBase);
            }
            for (auto &vertex : value.pinnedVertices)
                if (vertex >= 0)
                    vertex += static_cast<std::int32_t>(vertexBase);
            if (!transaction.addSoftBody(std::move(value)))
                return false;
        }
        return true;
    }, "モデルを追加結合");
    if (!result.success)
        return false;
    local.addedVertices = other.vertices.size();
    local.addedMaterials = other.materials.size();
    local.addedBones = other.bones.size();
    local.addedMorphs = other.morphs.size();
    if (report != nullptr)
        *report = std::move(local);
    return true;
}

} // namespace pmxer
