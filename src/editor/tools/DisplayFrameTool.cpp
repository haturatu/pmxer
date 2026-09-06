#include "DisplayFrameTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool addBoneToDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle frame, mmd::BoneHandle bone) {
    const auto *source = session.document.resolve(frame);
    const auto *target = session.document.resolve(bone);
    if (source == nullptr || target == nullptr)
        return false;
    auto edited = *source;
    edited.items.push_back({true, static_cast<std::int32_t>(target - session.document.model().bones.data())});
    return editDisplayFrame(session, frame, edited).success;
}

bool addMorphToDisplayFrame(DocumentSession &session, mmd::DisplayFrameHandle frame, mmd::MorphHandle morph) {
    const auto *source = session.document.resolve(frame);
    const auto *target = session.document.resolve(morph);
    if (source == nullptr || target == nullptr)
        return false;
    auto edited = *source;
    edited.items.push_back({false, static_cast<std::int32_t>(target - session.document.model().morphs.data())});
    return editDisplayFrame(session, frame, edited).success;
}

bool moveDisplayItem(DocumentSession &session, mmd::DisplayFrameHandle frame, std::size_t from, std::size_t to) {
    const auto *source = session.document.resolve(frame);
    if (source == nullptr || from >= source->items.size() || to >= source->items.size())
        return false;
    auto edited = *source;
    auto value = edited.items[from];
    edited.items.erase(edited.items.begin() + static_cast<std::ptrdiff_t>(from));
    edited.items.insert(edited.items.begin() + static_cast<std::ptrdiff_t>(to), value);
    return editDisplayFrame(session, frame, edited).success;
}

} // namespace pmxer
