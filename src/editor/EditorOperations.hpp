#pragma once

#include "DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <functional>
#include <string>

namespace pmxer {

struct OperationResult {
    bool success{};
    std::string message;
};

[[nodiscard]] OperationResult editVertex(DocumentSession &, mmd::VertexHandle, const mmd::PmxVertex &);
[[nodiscard]] OperationResult editMaterial(DocumentSession &, mmd::MaterialHandle, const mmd::PmxMaterial &);
[[nodiscard]] OperationResult editBone(DocumentSession &, mmd::BoneHandle, const mmd::PmxBone &);
[[nodiscard]] OperationResult editMorph(DocumentSession &, mmd::MorphHandle, const mmd::PmxMorph &);
[[nodiscard]] OperationResult editDisplayFrame(DocumentSession &, mmd::DisplayFrameHandle, const mmd::PmxDisplayFrame &);
[[nodiscard]] OperationResult editRigidBody(DocumentSession &, mmd::RigidBodyHandle, const mmd::PmxRigidBody &);
[[nodiscard]] OperationResult editJoint(DocumentSession &, mmd::JointHandle, const mmd::PmxJoint &);
[[nodiscard]] OperationResult editSoftBody(DocumentSession &, mmd::SoftBodyHandle, const mmd::PmxSoftBody &);

[[nodiscard]] OperationResult normalizeWeights(DocumentSession &, float threshold = 0.001F);
[[nodiscard]] OperationResult setVertexSkin(DocumentSession &, mmd::VertexHandle, const mmd::PmxVertex &);

} // namespace pmxer

