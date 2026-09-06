#include "VertexTool.hpp"

#include "../EditorOperations.hpp"

namespace pmxer {

bool setVertexPosition(DocumentSession &session, mmd::VertexHandle handle, mmd::Float3 value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setVertexPosition(handle, value);
    }, "頂点位置を変更").success;
}

bool setVertexNormal(DocumentSession &session, mmd::VertexHandle handle, mmd::Float3 value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setVertexNormal(handle, value);
    }, "頂点法線を変更").success;
}

bool setVertexUv(DocumentSession &session, mmd::VertexHandle handle, mmd::Float2 value) {
    return applyTransaction(session, [&](auto &transaction) {
        return transaction.setVertexUv(handle, value);
    }, "頂点UVを変更").success;
}

bool setVertexWeightType(DocumentSession &session, mmd::VertexHandle handle, mmd::PmxWeightType type) {
    return applyTransaction(session, [&](auto &transaction) {
        const auto *source = session.document.resolve(handle);
        if (source == nullptr)
            return false;
        auto value = *source;
        value.weightType = type;
        return transaction.setVertexSkin(handle, value);
    }, "頂点ウェイト種別を変更").success;
}

} // namespace pmxer

