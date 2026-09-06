#include "BoneTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool setBoneParent(DocumentSession &session, mmd::BoneHandle child, mmd::BoneHandle parent) {
    const auto *source = session.document.resolve(child);
    const auto *target = session.document.resolve(parent);
    if (source == nullptr || target == nullptr)
        return false;
    auto edited = *source;
    edited.parent = static_cast<std::int32_t>(target - session.document.model().bones.data());
    return editBone(session, child, edited).success;
}

bool setBoneIkTarget(DocumentSession &session, mmd::BoneHandle bone, mmd::BoneHandle target) {
    const auto *source = session.document.resolve(bone);
    const auto *targetBone = session.document.resolve(target);
    if (source == nullptr || targetBone == nullptr)
        return false;
    auto edited = *source;
    edited.ikTarget = static_cast<std::int32_t>(targetBone - session.document.model().bones.data());
    return editBone(session, bone, edited).success;
}

bool reorderBone(DocumentSession &session, mmd::BoneHandle bone, std::size_t destination) {
    return moveBone(session, bone, destination).success;
}

bool addIkLink(DocumentSession &session, mmd::BoneHandle bone, mmd::PmxIkLink link) {
    const auto *source = session.document.resolve(bone);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.ikLinks.push_back(std::move(link));
    return editBone(session, bone, edited).success;
}

} // namespace pmxer
