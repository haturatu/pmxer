#include "ViewportGizmo.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/UiStatus.hpp"
#include "../editor/EditorOperations.hpp"

#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <optional>
#include <string>

namespace pmxer {
namespace {

constexpr float radiansToDegrees = 57.2957795F;
constexpr float degreesToRadians = 0.0174532925F;

std::array<float, 16> transpose(const std::array<float, 16> &source) {
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4U; ++row)
        for (std::size_t column = 0; column < 4U; ++column)
            result[column * 4U + row] = source[row * 4U + column];
    return result;
}

struct TransformSource {
    mmd::Float3 position{};
    mmd::Float3 rotation{};
};

std::optional<TransformSource> sourceFor(const DocumentSession &session,
                                         const SelectionItem &selected) {
    if (selected.kind == SelectionKind::vertex) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::VertexTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, {}};
    } else if (selected.kind == SelectionKind::bone) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::BoneTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, {}};
    } else if (selected.kind == SelectionKind::rigidBody) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::RigidBodyTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, value->rotation};
    } else if (selected.kind == SelectionKind::joint) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::JointTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, value->rotation};
    }
    return std::nullopt;
}

void compose(const TransformSource &source, std::array<float, 16> &matrix) {
    const float translation[]{source.position[0], source.position[1], source.position[2]};
    const float rotation[]{source.rotation[0] * radiansToDegrees,
                           source.rotation[1] * radiansToDegrees,
                           source.rotation[2] * radiansToDegrees};
    constexpr float scale[]{1.0F, 1.0F, 1.0F};
    ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale,
                                            matrix.data());
}

void decompose(const std::array<float, 16> &matrix, mmd::Float3 &position,
               mmd::Float3 &rotation, mmd::Float3 &scale) {
    float translationValues[3]{};
    float rotationValues[3]{};
    float scaleValues[3]{};
    ImGuizmo::DecomposeMatrixToComponents(matrix.data(), translationValues,
                                          rotationValues, scaleValues);
    position = {translationValues[0], translationValues[1],
                translationValues[2]};
    rotation = {rotationValues[0] * degreesToRadians,
                rotationValues[1] * degreesToRadians,
                rotationValues[2] * degreesToRadians};
    scale = {scaleValues[0], scaleValues[1], scaleValues[2]};
}

void commit(DocumentSession &session, const SelectionItem &selected,
            const TransformCapabilities &capabilities) {
    mmd::Float3 position{};
    mmd::Float3 rotation{};
    mmd::Float3 scale{};
    decompose(session.ui.gizmoMatrix, position, rotation, scale);
    OperationResult result;
    if (selected.kind == SelectionKind::vertex) {
        const auto handle =
            selectionHandle<mmd::VertexTag>(session.document, selected);
        const auto *resolved = session.document.resolve(handle);
        if (resolved == nullptr) {
            setStatus(session, "選択した頂点は無効です", UiStatusKind::error,
                      std::chrono::milliseconds::zero(), true);
            return;
        }
        auto value = *resolved;
        value.position = position;
        result = editVertex(session, handle, value);
    } else if (selected.kind == SelectionKind::bone) {
        const auto handle =
            selectionHandle<mmd::BoneTag>(session.document, selected);
        const auto *resolved = session.document.resolve(handle);
        if (resolved == nullptr) {
            setStatus(session, "選択したボーンは無効です", UiStatusKind::error,
                      std::chrono::milliseconds::zero(), true);
            return;
        }
        auto value = *resolved;
        value.position = position;
        result = editBone(session, handle, value);
    } else if (selected.kind == SelectionKind::rigidBody) {
        const auto handle =
            selectionHandle<mmd::RigidBodyTag>(session.document, selected);
        const auto *resolved = session.document.resolve(handle);
        if (resolved == nullptr) {
            setStatus(session, "選択した剛体は無効です", UiStatusKind::error,
                      std::chrono::milliseconds::zero(), true);
            return;
        }
        auto value = *resolved;
        value.position = position;
        if (capabilities.rotate)
            value.rotation = rotation;
        if (capabilities.scale)
            for (std::size_t index = 0; index < value.size.size(); ++index)
                value.size[index] *= std::max(std::abs(scale[index]), 0.001F);
        result = editRigidBody(session, handle, value);
    } else if (selected.kind == SelectionKind::joint) {
        const auto handle =
            selectionHandle<mmd::JointTag>(session.document, selected);
        const auto *resolved = session.document.resolve(handle);
        if (resolved == nullptr) {
            setStatus(session, "選択したジョイントは無効です", UiStatusKind::error,
                      std::chrono::milliseconds::zero(), true);
            return;
        }
        auto value = *resolved;
        value.position = position;
        if (capabilities.rotate)
            value.rotation = rotation;
        result = editJoint(session, handle, value);
    }
    if (selected.kind == SelectionKind::vertex ||
        selected.kind == SelectionKind::bone ||
        selected.kind == SelectionKind::rigidBody ||
        selected.kind == SelectionKind::joint)
        setStatus(session,
                  result.success ? std::string{"ビューポート操作を適用しました"}
                                 : result.message,
                  result.success ? UiStatusKind::success : UiStatusKind::error,
                  result.success ? std::chrono::seconds(4)
                                 : std::chrono::milliseconds::zero(),
                  !result.success);
}

} // namespace

std::optional<ImGuizmo::OPERATION>
operationFor(ViewportTool tool, const TransformCapabilities &capabilities) noexcept {
    switch (tool) {
    case ViewportTool::select:
    case ViewportTool::move:
        return capabilities.move ? std::optional{ImGuizmo::TRANSLATE} : std::nullopt;
    case ViewportTool::rotate:
        return capabilities.rotate ? std::optional{ImGuizmo::ROTATE} : std::nullopt;
    case ViewportTool::scale:
        return capabilities.scale ? std::optional{ImGuizmo::SCALE} : std::nullopt;
    }
    return std::nullopt;
}

void drawViewportGizmo(DocumentSession &session, const CameraMatrices &camera,
                       ImVec2 origin, ImVec2 size) {
    if (session.ui.viewportTool == ViewportTool::select ||
        session.selection.items().size() != 1U)
        return;
    const auto selected = session.selection.items().front();
    const auto capabilities = transformCapabilities(session);
    const auto operation = operationFor(session.ui.viewportTool, capabilities);
    if (!operation)
        return;
    const auto source = sourceFor(session, selected);
    if (!source)
        return;
    if (session.ui.gizmoSelection != selected || !session.ui.gizmoDragging) {
        session.ui.gizmoSelection = selected;
        compose(*source, session.ui.gizmoMatrix);
    }

    auto view = transpose(camera.view);
    auto projection = transpose(camera.projection);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::SetOrthographic(session.ui.orthographic);
    const auto mode = session.ui.localTransform ? ImGuizmo::LOCAL
                                                : ImGuizmo::WORLD;
    const float snap[] = {
        *operation == ImGuizmo::ROTATE ? 5.0F : 0.1F,
        *operation == ImGuizmo::ROTATE ? 5.0F : 0.1F,
        *operation == ImGuizmo::ROTATE ? 5.0F : 0.1F,
    };
    (void)ImGuizmo::Manipulate(
        view.data(), projection.data(), *operation, mode,
        session.ui.gizmoMatrix.data(), nullptr,
        session.ui.snapTransform ? snap : nullptr);
    const auto usingGizmo = ImGuizmo::IsUsing();
    if (session.ui.gizmoDragging && !usingGizmo)
        commit(session, selected, capabilities);
    session.ui.gizmoDragging = usingGizmo;
}

} // namespace pmxer
