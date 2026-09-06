#include "PhysicsTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

PhysicsPreviewStatus physicsPreviewStatus(const mmd::PmxModel &model) {
    PhysicsPreviewStatus result;
    result.softBodies = model.softBodies.size();
    for (const auto &joint : model.joints) {
        if (joint.type == 0)
            ++result.supportedJoints;
        else
            ++result.preservedJoints;
    }
    return result;
}

bool setJointType(DocumentSession &session, mmd::JointHandle handle, std::uint8_t type) {
    return applyTransaction(session, [&](auto &transaction) {
        const auto *joint = session.document.resolve(handle);
        if (joint == nullptr)
            return false;
        auto value = *joint;
        value.type = type;
        return transaction.setJoint(handle, value);
    }, "ジョイント種別を変更").success;
}

bool generateRigidBodyChain(DocumentSession &session, const std::vector<mmd::BoneHandle> &bones, std::uint8_t shape) {
    return applyTransaction(session, [&](auto &transaction) {
        for (const auto boneHandle : bones) {
            const auto *bone = session.document.resolve(boneHandle);
            if (bone == nullptr)
                return false;
            mmd::PmxRigidBody body;
            body.name = bone->name + "_body";
            body.englishName = body.name;
            body.shape = shape;
            body.size = {0.1F, 0.1F, 0.1F};
            body.mass = 1.0F;
            body.linearDamping = 0.5F;
            body.angularDamping = 0.5F;
            if (!transaction.addRigidBody(mmd::RigidBodyDraft{std::move(body), boneHandle}))
                return false;
        }
        return true;
    }, "剛体チェーンを生成").success;
}

} // namespace pmxer
