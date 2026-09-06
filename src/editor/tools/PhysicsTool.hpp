#pragma once

#include "../DocumentSession.hpp"

#include <mmd/pmx.hpp>

#include <string>
#include <vector>

namespace pmxer {

struct PhysicsPreviewStatus {
    std::size_t supportedJoints{};
    std::size_t preservedJoints{};
    std::size_t softBodies{};
};

[[nodiscard]] PhysicsPreviewStatus physicsPreviewStatus(const mmd::PmxModel &);
[[nodiscard]] bool setJointType(DocumentSession &, mmd::JointHandle, std::uint8_t);
[[nodiscard]] bool generateRigidBodyChain(DocumentSession &, const std::vector<mmd::BoneHandle> &, std::uint8_t shape);

} // namespace pmxer

