#include "VertexTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool setVertexPosition(DocumentSession &session, mmd::VertexHandle handle, mmd::Float3 value) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.position = value;
    return editVertex(session, handle, edited).success;
}

bool setVertexNormal(DocumentSession &session, mmd::VertexHandle handle, mmd::Float3 value) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.normal = value;
    return editVertex(session, handle, edited).success;
}

bool setVertexUv(DocumentSession &session, mmd::VertexHandle handle, mmd::Float2 value) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.uv = value;
    return editVertex(session, handle, edited).success;
}

bool setVertexWeightType(DocumentSession &session, mmd::VertexHandle handle, mmd::PmxWeightType type) {
    const auto *source = session.document.resolve(handle);
    if (source == nullptr)
        return false;
    auto edited = *source;
    edited.weightType = type;
    return editVertex(session, handle, edited).success;
}

} // namespace pmxer
