#include "EditorPanels.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/DiffController.hpp"
#include "../editor/EditorDiagnostics.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/ReferenceInspector.hpp"
#include "../editor/RecoveryController.hpp"
#include "../editor/SaveController.hpp"
#include "../editor/tools/PhysicsTool.hpp"
#include "../editor/tools/TextureTool.hpp"
#include "../preview/PreviewController.hpp"
#include "ViewportPanel.hpp"

#include <imgui.h>
#include <mmd/vmd.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace pmxer {
namespace {

struct PreviewUi {
    const mmd::PmxDocument *document{};
    std::filesystem::path source;
    std::uint64_t revision{std::numeric_limits<std::uint64_t>::max()};
    std::unique_ptr<PreviewController> controller;
    std::optional<mmd::VmdMotion> motion;
    std::optional<mmd::VpdPose> pose;
    std::optional<mmd::AnimatedModelFrame> frame;
};

PreviewUi &previewUi() {
    static PreviewUi state;
    return state;
}

PreviewUi &updatePreview(DocumentSession &session) {
    auto &state = previewUi();
    if (state.source != session.path) {
        state.motion.reset();
        state.pose.reset();
        state.frame.reset();
    }
    if (!state.controller || state.document != &session.document || state.source != session.path ||
        state.revision != session.revision) {
        state.controller = std::make_unique<PreviewController>(session.document.model());
        state.document = &session.document;
        state.source = session.path;
        state.revision = session.revision;
        if (state.motion)
            state.controller->setMotion(&*state.motion);
        if (state.pose)
            state.controller->setPose(&*state.pose);
        state.controller->setPhysicsEnabled(session.previewPhysics);
        state.controller->setIkEnabled(session.previewIk);
        state.frame = state.controller->evaluate();
    } else {
        state.controller->setPhysicsEnabled(session.previewPhysics);
        state.controller->setIkEnabled(session.previewIk);
    }
    if (session.ui.previewPlaying)
        state.frame = state.controller->evaluate(1.0F / 60.0F);
    return state;
}

int resizeTextCallback(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;
    auto *value = static_cast<std::string *>(data->UserData);
    value->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = value->data();
    return 0;
}

bool inputString(const char *label, std::string &value) {
    if (value.capacity() < value.size() + 1)
        value.reserve(value.size() + 1);
    value.resize(value.size());
    const auto changed = ImGui::InputText(label, value.data(), value.capacity() + 1,
                                          ImGuiInputTextFlags_CallbackResize, resizeTextCallback, &value);
    value.resize(std::strlen(value.c_str()));
    return changed;
}

bool selectIndex(const char *label, std::size_t count, std::size_t &index) {
    if (count == 0) {
        ImGui::TextUnformatted("対象がありません");
        return false;
    }
    index = std::min(index, count - 1);
    int value = static_cast<int>(index);
    const auto changed = ImGui::InputInt(label, &value);
    value = std::clamp(value, 0, static_cast<int>(count - 1));
    index = static_cast<std::size_t>(value);
    return changed;
}

const char *weightName(mmd::PmxWeightType type) {
    switch (type) {
    case mmd::PmxWeightType::bdef1:
        return "BDEF1";
    case mmd::PmxWeightType::bdef2:
        return "BDEF2";
    case mmd::PmxWeightType::bdef4:
        return "BDEF4";
    case mmd::PmxWeightType::sdef:
        return "SDEF";
    case mmd::PmxWeightType::qdef:
        return "QDEF";
    }
    return "不明";
}

bool chooseBone(const char *label, const mmd::PmxModel &model, std::int32_t &index, bool allowNone = true) {
    const std::string current = index >= 0 && static_cast<std::size_t>(index) < model.bones.size()
                                    ? model.bones[static_cast<std::size_t>(index)].name
                                    : (allowNone ? "なし" : "未選択");
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (allowNone && ImGui::Selectable("なし", index < 0)) {
            index = -1;
            changed = true;
        }
        for (std::size_t i = 0; i < model.bones.size(); ++i) {
            const bool selected = index == static_cast<std::int32_t>(i);
            if (ImGui::Selectable(model.bones[i].name.c_str(), selected)) {
                index = static_cast<std::int32_t>(i);
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool chooseTexture(const char *label, const mmd::PmxModel &model, std::int32_t &index) {
    const std::string current = index >= 0 && static_cast<std::size_t>(index) < model.textures.size()
                                    ? model.textures[static_cast<std::size_t>(index)].storedPath
                                    : "なし";
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("なし", index < 0)) {
            index = -1;
            changed = true;
        }
        for (std::size_t i = 0; i < model.textures.size(); ++i) {
            const bool selected = index == static_cast<std::int32_t>(i);
            if (ImGui::Selectable(model.textures[i].storedPath.c_str(), selected)) {
                index = static_cast<std::int32_t>(i);
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawModelPanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("モデル");
    ImGui::Text("頂点 %zu / 面 %zu", model.vertices.size(), model.indices.size() / 3);
    ImGui::Text("材質 %zu / テクスチャ %zu", model.materials.size(), model.textures.size());
    ImGui::Text("ボーン %zu / モーフ %zu", model.bones.size(), model.morphs.size());
    ImGui::Text("剛体 %zu / ジョイント %zu / ソフトボディ %zu", model.rigidBodies.size(), model.joints.size(),
                model.softBodies.size());
    ImGui::Text("形式 %.1f / 頂点添付UV %u", model.metadata.version, model.metadata.additionalUvCount);
    if (!session.ui.metadataDraft)
        session.ui.metadataDraft = model.metadata;
    auto &metadata = *session.ui.metadataDraft;
    inputString("モデル名", metadata.modelName);
    inputString("モデル英語名", metadata.englishName);
    inputString("コメント", metadata.comment);
    inputString("英語コメント", metadata.englishComment);
    if (ImGui::Button("モデル情報を適用")) {
        session.ui.status = editMetadata(session, metadata).success ? "モデル情報を更新しました" : "モデル情報更新に失敗しました";
        session.ui.metadataDraft.reset();
    }
    ImGui::Separator();
    inputString("開くパス", session.ui.openPath);
    if (ImGui::Button("開く") && !session.ui.openPath.empty()) {
        try {
            const auto path = std::filesystem::path(session.ui.openPath);
            auto loaded = mmd::pmx::load(path);
            session = DocumentSession(std::move(loaded), path);
            session.ui.openPath = path.string();
            session.ui.status = "読み込みました";
            ImGui::End();
            return;
        } catch (const std::exception &error) {
            session.ui.status = error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存"))
        session.ui.status = saveDocument(session).success ? "保存しました" : "保存に失敗しました";
    ImGui::SameLine();
    if (ImGui::Button("回復保存"))
        session.ui.status = writeRecovery(session).success ? "回復情報を保存しました" : "回復保存に失敗しました";
    ImGui::Separator();
    auto &preview = previewUi();
    ImGui::Checkbox("再生", &session.ui.previewPlaying);
    inputString("モーション", session.ui.motionPath);
    if (ImGui::Button("モーション読込") && !session.ui.motionPath.empty()) {
        try {
            preview.motion = mmd::vmd::load(session.ui.motionPath);
            preview.pose.reset();
            preview.controller->setMotion(&*preview.motion);
            session.ui.status = "モーションを読み込みました";
        } catch (const std::exception &error) {
            session.ui.status = error.what();
        }
    }
    ImGui::SameLine();
    inputString("ポーズ", session.ui.posePath);
    if (ImGui::Button("ポーズ読込") && !session.ui.posePath.empty()) {
        try {
            preview.pose = mmd::vpd::load(session.ui.posePath);
            preview.motion.reset();
            preview.controller->setPose(&*preview.pose);
            session.ui.status = "ポーズを読み込みました";
        } catch (const std::exception &error) {
            session.ui.status = error.what();
        }
    }
    if (!session.ui.status.empty())
        ImGui::TextWrapped("状態: %s", session.ui.status.c_str());
    ImGui::Text("変更済み: %s / Undo %zu / Redo %zu", session.modified ? "はい" : "いいえ",
                session.commands.undoCount(), session.commands.redoCount());
    const auto differences = compareWithBaseline(session);
    ImGui::Text("基準との差分: %zu", differences.differences.size());
    ImGui::End();
}

void drawVertexPanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("頂点");
    if (selectIndex("番号", model.vertices.size(), session.ui.vertexIndex))
        session.ui.vertexDraft.reset();
    if (model.vertices.empty()) {
        ImGui::End();
        return;
    }
    const auto handle = session.document.vertexHandle(session.ui.vertexIndex);
    if (!session.ui.vertexDraft)
        session.ui.vertexDraft = *session.document.resolve(handle);
    auto &draft = *session.ui.vertexDraft;
    ImGui::Text("ウェイト: %s", weightName(draft.weightType));
    ImGui::InputFloat3("位置", draft.position.data());
    ImGui::InputFloat3("法線", draft.normal.data());
    ImGui::InputFloat2("UV", draft.uv.data());
    ImGui::InputFloat("エッジ倍率", &draft.edgeScale);
    if (ImGui::BeginCombo("方式", weightName(draft.weightType))) {
        const std::array types{mmd::PmxWeightType::bdef1, mmd::PmxWeightType::bdef2, mmd::PmxWeightType::bdef4,
                               mmd::PmxWeightType::sdef, mmd::PmxWeightType::qdef};
        for (const auto type : types) {
            const bool selected = draft.weightType == type;
            if (ImGui::Selectable(weightName(type), selected))
                draft.weightType = type;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const auto count = draft.weightType == mmd::PmxWeightType::bdef1
                           ? 1U
                           : (draft.weightType == mmd::PmxWeightType::bdef2 || draft.weightType == mmd::PmxWeightType::sdef)
                                 ? 2U
                                 : 4U;
    for (std::size_t i = 0; i < count; ++i) {
        const auto label = "ボーン " + std::to_string(i + 1);
        chooseBone(label.c_str(), model, draft.bones[i], false);
        const auto weightLabel = "重み " + std::to_string(i + 1);
        ImGui::InputFloat(weightLabel.c_str(), &draft.weights[i]);
    }
    if (draft.weightType == mmd::PmxWeightType::sdef) {
        ImGui::InputFloat3("SDEF C", draft.sdefC.data());
        ImGui::InputFloat3("SDEF R0", draft.sdefR0.data());
        ImGui::InputFloat3("SDEF R1", draft.sdefR1.data());
    }
    if (ImGui::Button("適用")) {
        session.ui.status = editVertex(session, handle, draft).success ? "頂点を更新しました" : "頂点更新に失敗しました";
        session.ui.vertexDraft.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("ウェイト正規化"))
        session.ui.status = normalizeWeights(session).success ? "ウェイトを正規化しました" : "正規化に失敗しました";
    ImGui::End();
}

void drawMaterialPanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("材質");
    if (selectIndex("番号", model.materials.size(), session.ui.materialIndex))
        session.ui.materialDraft.reset();
    if (model.materials.empty()) {
        ImGui::End();
        return;
    }
    const auto handle = session.document.materialHandle(session.ui.materialIndex);
    if (!session.ui.materialDraft)
        session.ui.materialDraft = *session.document.resolve(handle);
    auto &draft = *session.ui.materialDraft;
    inputString("名前", draft.name);
    inputString("英語名", draft.englishName);
    ImGui::ColorEdit4("拡散色", draft.diffuse.data());
    ImGui::ColorEdit3("鏡面色", draft.specular.data());
    ImGui::InputFloat("光沢", &draft.shininess);
    ImGui::ColorEdit3("環境色", draft.ambient.data());
    ImGui::ColorEdit4("輪郭色", draft.edgeColor.data());
    ImGui::InputFloat("輪郭幅", &draft.edgeSize);
    chooseTexture("テクスチャ", model, draft.textureIndex);
    chooseTexture("球テクスチャ", model, draft.sphereTextureIndex);
    chooseTexture("トゥーン", model, draft.toonTextureIndex);
    int flags = draft.drawFlags;
    ImGui::InputInt("描画フラグ", &flags);
    draft.drawFlags = static_cast<std::uint8_t>(std::clamp(flags, 0, 255));
    if (ImGui::Button("適用")) {
        session.ui.status = editMaterial(session, handle, draft).success ? "材質を更新しました" : "材質更新に失敗しました";
        session.ui.materialDraft.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("←") && session.ui.materialIndex > 0) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveMaterial(handle, session.ui.materialIndex - 1);
        }, "材質を前へ移動");
        if (result.success)
            --session.ui.materialIndex;
    }
    ImGui::SameLine();
    if (ImGui::Button("→") && session.ui.materialIndex + 1 < model.materials.size()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveMaterial(handle, session.ui.materialIndex + 1);
        }, "材質を後へ移動");
        if (result.success)
            ++session.ui.materialIndex;
    }
    ImGui::End();
}

void drawTexturePanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("テクスチャ");
    if (selectIndex("番号", model.textures.size(), session.ui.textureIndex)) {
        const auto handle = session.document.textureHandle(session.ui.textureIndex);
        session.selection.set({SelectionKind::texture, handle.id, handle.generation});
    }
    if (!model.textures.empty()) {
        const auto texture = session.document.textureHandle(session.ui.textureIndex);
        ImGui::Text("保存パス: %s", model.textures[session.ui.textureIndex].storedPath.c_str());
        static std::string replacement;
        if (replacement.empty())
            replacement = model.textures[session.ui.textureIndex].storedPath;
        inputString("新しいパス", replacement);
        if (ImGui::Button("再リンク")) {
            session.ui.status = relinkTexture(session, texture, replacement) ? "テクスチャを更新しました" : "更新に失敗しました";
            replacement.clear();
        }
        const auto missing = missingTextures(model);
        ImGui::Text("不足: %zu", missing.size());
        if (ImGui::Button("絶対パスを相対化") && !session.path.empty())
            session.ui.status = convertAbsoluteTextures(session, session.path.parent_path()) ? "パスを変換しました" : "変換に失敗しました";
    }
    ImGui::End();
}

void drawBonePanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("ボーン");
    if (selectIndex("番号", model.bones.size(), session.ui.boneIndex))
        session.ui.boneDraft.reset();
    if (model.bones.empty()) {
        ImGui::End();
        return;
    }
    const auto handle = session.document.boneHandle(session.ui.boneIndex);
    if (!session.ui.boneDraft)
        session.ui.boneDraft = *session.document.resolve(handle);
    auto &draft = *session.ui.boneDraft;
    inputString("名前", draft.name);
    inputString("英語名", draft.englishName);
    ImGui::InputFloat3("位置", draft.position.data());
    ImGui::InputInt("変形層", &draft.deformLayer);
    ImGui::InputScalar("フラグ", ImGuiDataType_U16, &draft.flags);
    chooseBone("親", model, draft.parent);
    if ((draft.flags & 1U) != 0)
        chooseBone("末端", model, draft.tailBone);
    if ((draft.flags & 0x0300U) != 0) {
        chooseBone("付与元", model, draft.inheritParent);
        ImGui::InputFloat("付与率", &draft.inheritRatio);
    }
    if ((draft.flags & 0x0020U) != 0) {
        chooseBone("IK対象", model, draft.ikTarget);
        ImGui::InputInt("IK回数", &draft.ikLoopCount);
        ImGui::InputFloat("IK角度", &draft.ikLimitAngle);
        ImGui::Text("IKリンク: %zu", draft.ikLinks.size());
    }
    if (ImGui::Button("適用")) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            const auto parent = draft.parent >= 0 && static_cast<std::size_t>(draft.parent) < model.bones.size()
                                    ? std::optional{session.document.boneHandle(static_cast<std::size_t>(draft.parent))}
                                    : std::nullopt;
            if (!transaction.setBoneName(handle, draft.name) || !transaction.setBoneEnglishName(handle, draft.englishName) ||
                !transaction.setBonePosition(handle, draft.position) || !transaction.setBoneDeformLayer(handle, draft.deformLayer) ||
                !transaction.setBoneFlags(handle, draft.flags) || !transaction.setBoneParent(handle, parent))
                return false;
            if (draft.tailBone >= 0 && static_cast<std::size_t>(draft.tailBone) < model.bones.size() &&
                !transaction.setBoneTailBone(handle, session.document.boneHandle(static_cast<std::size_t>(draft.tailBone))))
                return false;
            if ((draft.flags & 0x0300U) != 0 &&
                !transaction.setBoneInheritParent(handle, draft.inheritParent >= 0
                                                             ? std::optional{session.document.boneHandle(static_cast<std::size_t>(draft.inheritParent))}
                                                             : std::nullopt))
                return false;
            if ((draft.flags & 0x0020U) != 0 &&
                !transaction.setBoneIkTarget(handle, draft.ikTarget >= 0
                                                       ? std::optional{session.document.boneHandle(static_cast<std::size_t>(draft.ikTarget))}
                                                       : std::nullopt))
                return false;
            return transaction.setBoneIkLimits(handle, draft.ikLoopCount, draft.ikLimitAngle);
        }, "ボーンを更新");
        session.ui.status = result.success ? "ボーンを更新しました" : result.message;
        session.ui.boneDraft.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("←") && session.ui.boneIndex > 0) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveBone(handle, session.ui.boneIndex - 1);
        }, "ボーンを前へ移動");
        if (result.success)
            --session.ui.boneIndex;
    }
    ImGui::SameLine();
    if (ImGui::Button("→") && session.ui.boneIndex + 1 < model.bones.size()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveBone(handle, session.ui.boneIndex + 1);
        }, "ボーンを後へ移動");
        if (result.success)
            ++session.ui.boneIndex;
    }
    ImGui::End();
}

void drawMorphPanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("モーフ");
    if (selectIndex("番号", model.morphs.size(), session.ui.morphIndex))
        session.ui.morphDraft.reset();
    if (model.morphs.empty()) {
        ImGui::End();
        return;
    }
    const auto handle = session.document.morphHandle(session.ui.morphIndex);
    if (!session.ui.morphDraft)
        session.ui.morphDraft = *session.document.resolve(handle);
    auto &draft = *session.ui.morphDraft;
    inputString("名前", draft.name);
    inputString("英語名", draft.englishName);
    int panel = draft.panel;
    int type = draft.type;
    ImGui::InputInt("パネル", &panel);
    ImGui::InputInt("種類", &type);
    draft.panel = static_cast<std::uint8_t>(std::clamp(panel, 0, 4));
    draft.type = static_cast<std::uint8_t>(std::clamp(type, 0, 10));
    ImGui::Text("オフセット: %zu", draft.offsets.size());
    if (ImGui::Button("適用")) {
        session.ui.status = editMorph(session, handle, draft).success ? "モーフを更新しました" : "モーフ更新に失敗しました";
        session.ui.morphDraft.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("←") && session.ui.morphIndex > 0) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveMorph(handle, session.ui.morphIndex - 1);
        }, "モーフを前へ移動");
        if (result.success)
            --session.ui.morphIndex;
    }
    ImGui::SameLine();
    if (ImGui::Button("→") && session.ui.morphIndex + 1 < model.morphs.size()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveMorph(handle, session.ui.morphIndex + 1);
        }, "モーフを後へ移動");
        if (result.success)
            ++session.ui.morphIndex;
    }
    ImGui::End();
}

void drawDisplayFramePanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("表示枠");
    if (selectIndex("番号", model.displayFrames.size(), session.ui.displayFrameIndex))
        session.ui.displayFrameDraft.reset();
    if (model.displayFrames.empty()) {
        ImGui::End();
        return;
    }
    const auto handle = session.document.displayFrameHandle(session.ui.displayFrameIndex);
    if (!session.ui.displayFrameDraft)
        session.ui.displayFrameDraft = *session.document.resolve(handle);
    auto &draft = *session.ui.displayFrameDraft;
    inputString("名前", draft.name);
    inputString("英語名", draft.englishName);
    ImGui::Text("項目: %zu", draft.items.size());
    for (const auto &item : draft.items)
        ImGui::BulletText("%s %d", item.bone ? "ボーン" : "モーフ", item.index);
    if (ImGui::Button("適用")) {
        session.ui.status = editDisplayFrame(session, handle, draft).success ? "表示枠を更新しました" : "表示枠更新に失敗しました";
        session.ui.displayFrameDraft.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("←") && session.ui.displayFrameIndex > 0) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveDisplayFrame(handle, session.ui.displayFrameIndex - 1);
        }, "表示枠を前へ移動");
        if (result.success)
            --session.ui.displayFrameIndex;
    }
    ImGui::SameLine();
    if (ImGui::Button("→") && session.ui.displayFrameIndex + 1 < model.displayFrames.size()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveDisplayFrame(handle, session.ui.displayFrameIndex + 1);
        }, "表示枠を後へ移動");
        if (result.success)
            ++session.ui.displayFrameIndex;
    }
    ImGui::End();
}

void drawPhysicsPanel(DocumentSession &session) {
    const auto &model = session.document.model();
    ImGui::Begin("物理");
    ImGui::Checkbox("物理プレビュー", &session.previewPhysics);
    ImGui::Checkbox("IKプレビュー", &session.previewIk);
    const auto status = physicsPreviewStatus(model);
    ImGui::Text("対応ジョイント %zu / 保存のみ %zu / ソフトボディ %zu", status.supportedJoints, status.preservedJoints,
                status.softBodies);
    if (ImGui::CollapsingHeader("剛体", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (selectIndex("剛体番号", model.rigidBodies.size(), session.ui.rigidBodyIndex))
            session.ui.rigidBodyDraft.reset();
        if (!model.rigidBodies.empty()) {
            const auto handle = session.document.rigidBodyHandle(session.ui.rigidBodyIndex);
            if (!session.ui.rigidBodyDraft)
                session.ui.rigidBodyDraft = *session.document.resolve(handle);
            auto &draft = *session.ui.rigidBodyDraft;
            inputString("剛体名", draft.name);
            chooseBone("関連ボーン", model, draft.bone);
            ImGui::InputFloat3("大きさ", draft.size.data());
            ImGui::InputFloat("質量", &draft.mass);
            ImGui::InputFloat("摩擦", &draft.friction);
            int mode = draft.mode;
            ImGui::InputInt("モード", &mode);
            draft.mode = static_cast<std::uint8_t>(std::clamp(mode, 0, 2));
            if (ImGui::Button("剛体を適用")) {
                session.ui.status = editRigidBody(session, handle, draft).success ? "剛体を更新しました" : "剛体更新に失敗しました";
                session.ui.rigidBodyDraft.reset();
            }
        }
    }
    if (ImGui::CollapsingHeader("ジョイント")) {
        if (selectIndex("ジョイント番号", model.joints.size(), session.ui.jointIndex))
            session.ui.jointDraft.reset();
        if (!model.joints.empty()) {
            const auto handle = session.document.jointHandle(session.ui.jointIndex);
            if (!session.ui.jointDraft)
                session.ui.jointDraft = *session.document.resolve(handle);
            auto &draft = *session.ui.jointDraft;
            inputString("ジョイント名", draft.name);
            int type = draft.type;
            ImGui::InputInt("種類", &type);
            draft.type = static_cast<std::uint8_t>(std::clamp(type, 0, 5));
            ImGui::InputFloat3("移動下限", draft.translationMinimum.data());
            ImGui::InputFloat3("移動上限", draft.translationMaximum.data());
            ImGui::InputFloat3("回転下限", draft.rotationMinimum.data());
            ImGui::InputFloat3("回転上限", draft.rotationMaximum.data());
            if (ImGui::Button("ジョイントを適用")) {
                session.ui.status = editJoint(session, handle, draft).success ? "ジョイントを更新しました" : "ジョイント更新に失敗しました";
                session.ui.jointDraft.reset();
            }
        }
    }
    if (ImGui::CollapsingHeader("ソフトボディ")) {
        if (selectIndex("ソフトボディ番号", model.softBodies.size(), session.ui.softBodyIndex))
            session.ui.softBodyDraft.reset();
        if (!model.softBodies.empty()) {
            const auto handle = session.document.softBodyHandle(session.ui.softBodyIndex);
            if (!session.ui.softBodyDraft)
                session.ui.softBodyDraft = *session.document.resolve(handle);
            auto &draft = *session.ui.softBodyDraft;
            inputString("ソフトボディ名", draft.name);
            ImGui::InputFloat("総質量", &draft.totalMass);
            ImGui::InputFloat("衝突余白", &draft.collisionMargin);
            ImGui::Text("アンカー %zu / 固定頂点 %zu", draft.anchors.size(), draft.pinnedVertices.size());
            if (ImGui::Button("ソフトボディを適用")) {
                session.ui.status = editSoftBody(session, handle, draft).success ? "ソフトボディを更新しました" : "ソフトボディ更新に失敗しました";
                session.ui.softBodyDraft.reset();
            }
        }
    }
    ImGui::End();
}

void drawDiagnosticsPanel(DocumentSession &session) {
    const auto detailed = validateForEditing(session.document.model());
    ImGui::Begin("診断");
    for (const auto &issue : detailed.issues) {
        const char *level = issue.severity >= mmd::ValidationSeverity::error ? "ERROR" : "WARN";
        ImGui::TextWrapped("[%s] %s: %s", level, issue.object.c_str(), issue.message.c_str());
    }
    if (detailed.issues.empty())
        ImGui::TextUnformatted("問題はありません");
    ImGui::End();
}

void drawReferencePanel(DocumentSession &session) {
    ImGui::Begin("参照");
    const auto &model = session.document.model();
    if (session.selection.items().empty()) {
        ImGui::TextUnformatted("ビューポートまたは各編集欄で対象を選択してください");
    } else {
        const auto selected = session.selection.items().front();
        ImGui::Text("選択 ID: %llu", static_cast<unsigned long long>(selected.id));
        ImGui::Text("世代: %u", selected.generation);
        if (selected.kind == SelectionKind::bone && !model.bones.empty()) {
            const auto handle = session.document.boneHandle(std::min(session.ui.boneIndex, model.bones.size() - 1));
            const auto summary = summarizeReferences(session.document, handle);
            ImGui::Text("頂点 %zu / 子ボーン %zu / IK %zu / モーフ %zu / 表示枠 %zu / 剛体 %zu",
                        summary.vertices, summary.childBones, summary.ikLinks, summary.morphs, summary.displayFrames,
                        summary.rigidBodies);
        }
    }
    ImGui::Text("頂点 %zu / 材質 %zu / ボーン %zu / モーフ %zu", model.vertices.size(), model.materials.size(),
                model.bones.size(), model.morphs.size());
    ImGui::End();
}

} // namespace

void drawEditorPanels(DocumentSession &session) {
    auto &preview = updatePreview(session);
    drawViewportPanel(session, preview.frame ? &*preview.frame : nullptr);
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("編集")) {
            if (ImGui::MenuItem("元に戻す", "Ctrl+Z", false, session.commands.undoCount() != 0))
                (void)session.undo();
            if (ImGui::MenuItem("やり直す", "Ctrl+Y", false, session.commands.redoCount() != 0))
                (void)session.redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("表示")) {
            ImGui::MenuItem("物理プレビュー", nullptr, &session.previewPhysics);
            ImGui::MenuItem("IKプレビュー", nullptr, &session.previewIk);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    drawModelPanel(session);
    drawVertexPanel(session);
    drawMaterialPanel(session);
    drawTexturePanel(session);
    drawBonePanel(session);
    drawMorphPanel(session);
    drawDisplayFramePanel(session);
    drawPhysicsPanel(session);
    drawDiagnosticsPanel(session);
    drawReferencePanel(session);
}

} // namespace pmxer
