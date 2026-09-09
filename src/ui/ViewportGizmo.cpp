#include "ViewportGizmo.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/DeformController.hpp"
#include "../editor/PreviewPoseQueries.hpp"
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

constexpr float degreesToRadians = 0.0174532925F;
constexpr mmd::Float4 identityQuaternion{0.0F, 0.0F, 0.0F, 1.0F};

std::array<float, 16> transpose(const std::array<float, 16> &source) {
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4U; ++row)
        for (std::size_t column = 0; column < 4U; ++column)
            result[column * 4U + row] = source[row * 4U + column];
    return result;
}

struct TransformSource {
    mmd::Float3 position{};
    mmd::Float4 rotation{0.0F, 0.0F, 0.0F, 1.0F};
};

mmd::Float4 normalizeQuaternion(mmd::Float4 value) {
    float length{};
    for (const auto component : value)
        length += component * component;
    if (length <= 1e-12F)
        return identityQuaternion;
    for (auto &component : value)
        component /= std::sqrt(length);
    return value;
}

mmd::Float4 multiplyQuaternion(const mmd::Float4 &lhs, const mmd::Float4 &rhs) {
    return normalizeQuaternion({
        lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
        lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
        lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2],
    });
}

mmd::Float4 quaternionFromEuler(const mmd::Float3 &euler) {
    const auto halfX = euler[0] * 0.5F;
    const auto halfY = euler[1] * 0.5F;
    const auto halfZ = euler[2] * 0.5F;
    const mmd::Float4 x{std::sin(halfX), 0.0F, 0.0F, std::cos(halfX)};
    const mmd::Float4 y{0.0F, std::sin(halfY), 0.0F, std::cos(halfY)};
    const mmd::Float4 z{0.0F, 0.0F, std::sin(halfZ), std::cos(halfZ)};
    return multiplyQuaternion(z, multiplyQuaternion(y, x));
}

std::optional<TransformSource> sourceFor(const DocumentSession &session,
                                         const SelectionItem &selected) {
    if (session.deform.active()) {
        const auto expected = session.deform.mode == DeformMode::shape
                                  ? SelectionKind::vertex
                                  : SelectionKind::bone;
        if (selected.kind != expected)
            return std::nullopt;
        if (session.deform.pivotMode == PivotMode::origin)
            return TransformSource{};
        if (session.deform.pivotMode == PivotMode::active || session.selection.items().size() == 1U) {
            if (expected == SelectionKind::vertex)
                return TransformSource{deformVertexPosition(
                    session, selectionHandle<mmd::VertexTag>(session.document, selected)),
                                        identityQuaternion};
            const auto handle = selectionHandle<mmd::BoneTag>(session.document, selected);
            return TransformSource{evaluatedBonePosition(session, handle, session.ui.previewFrame),
                                   evaluatedBoneRotation(session, handle, session.ui.previewFrame)};
        }
        mmd::Float3 center{};
        std::size_t count{};
        for (const auto &item : session.selection.items()) {
            if (item.kind != expected)
                continue;
            const auto position = expected == SelectionKind::vertex
                                      ? deformVertexPosition(
                                            session, selectionHandle<mmd::VertexTag>(session.document, item))
                                      : evaluatedBonePosition(
                                            session, selectionHandle<mmd::BoneTag>(session.document, item),
                                            session.ui.previewFrame);
            for (std::size_t component = 0; component < 3U; ++component)
                center[component] += position[component];
            ++count;
        }
        if (count == 0U)
            return std::nullopt;
        for (auto &component : center)
            component /= static_cast<float>(count);
        if (expected == SelectionKind::bone) {
            const auto handle = selectionHandle<mmd::BoneTag>(session.document, selected);
            return TransformSource{center, evaluatedBoneRotation(
                                            session, handle, session.ui.previewFrame)};
        }
        return TransformSource{center, identityQuaternion};
    }
    if (selected.kind == SelectionKind::vertex) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::VertexTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, identityQuaternion};
    } else if (selected.kind == SelectionKind::bone) {
        const auto handle = selectionHandle<mmd::BoneTag>(session.document, selected);
        if (session.document.resolve(handle) != nullptr)
            return TransformSource{evaluatedBonePosition(session, handle, session.ui.previewFrame),
                                   evaluatedBoneRotation(session, handle, session.ui.previewFrame)};
    } else if (selected.kind == SelectionKind::rigidBody) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::RigidBodyTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, quaternionFromEuler(value->rotation)};
    } else if (selected.kind == SelectionKind::joint) {
        const auto *value = session.document.resolve(
            selectionHandle<mmd::JointTag>(session.document, selected));
        if (value != nullptr)
            return TransformSource{value->position, quaternionFromEuler(value->rotation)};
    }
    return std::nullopt;
}

void compose(const TransformSource &source, std::array<float, 16> &matrix) {
    const auto rotation = normalizeQuaternion(source.rotation);
    const auto x = rotation[0];
    const auto y = rotation[1];
    const auto z = rotation[2];
    const auto w = rotation[3];
    matrix = {};
    matrix[0] = 1.0F - 2.0F * (y * y + z * z);
    matrix[4] = 2.0F * (x * y - z * w);
    matrix[8] = 2.0F * (x * z + y * w);
    matrix[1] = 2.0F * (x * y + z * w);
    matrix[5] = 1.0F - 2.0F * (x * x + z * z);
    matrix[9] = 2.0F * (y * z - x * w);
    matrix[2] = 2.0F * (x * z - y * w);
    matrix[6] = 2.0F * (y * z + x * w);
    matrix[10] = 1.0F - 2.0F * (x * x + y * y);
    matrix[12] = source.position[0];
    matrix[13] = source.position[1];
    matrix[14] = source.position[2];
    matrix[15] = 1.0F;
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
        const auto evaluated = evaluatedBonePosition(session, handle, session.ui.previewFrame);
        for (std::size_t component = 0; component < value.position.size(); ++component)
            value.position[component] += position[component] - evaluated[component];
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
        (session.selection.items().size() != 1U && !session.deform.active()))
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
    const auto startGizmoMatrix = session.ui.gizmoMatrix;
    std::array<float, 16> deltaMatrix{};
    (void)ImGuizmo::Manipulate(
        view.data(), projection.data(), *operation, mode,
        session.ui.gizmoMatrix.data(), deltaMatrix.data(),
        session.ui.snapTransform ? snap : nullptr);
    static_cast<void>(deltaMatrix);
    const auto usingGizmo = ImGuizmo::IsUsing();
    if (session.deform.active()) {
        if (usingGizmo && !session.ui.gizmoDragging)
            beginDeformGizmoDrag(session, startGizmoMatrix);
        if (usingGizmo)
            updateDeformGizmoDrag(session, session.ui.gizmoMatrix);
        if (usingGizmo)
            refreshDeformPreview(session);
    } else if (session.ui.gizmoDragging && !usingGizmo) {
        commit(session, selected, capabilities);
    }
    session.ui.gizmoDragging = usingGizmo;
}

} // namespace pmxer
