#include "EditorPanels.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/DiffController.hpp"
#include "../editor/EditorDiagnostics.hpp"
#include "../editor/EditorOperations.hpp"
#include "../editor/ReferenceInspector.hpp"
#include "../editor/RecoveryController.hpp"
#include "../editor/SaveController.hpp"
#include "../editor/tools/PhysicsTool.hpp"
#include "../editor/tools/ModelMergeTool.hpp"
#include "../editor/tools/SdefTool.hpp"
#include "../editor/tools/StandardBoneTool.hpp"
#include "../editor/tools/TextureTool.hpp"
#include "../preview/PreviewController.hpp"
#include "ViewportPanel.hpp"

#include <imgui.h>
#include <mmd/vmd.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

bool chooseMorph(const char *label, const mmd::PmxModel &model, std::int32_t &index) {
    const std::string current = index >= 0 && static_cast<std::size_t>(index) < model.morphs.size()
                                    ? model.morphs[static_cast<std::size_t>(index)].name
                                    : "なし";
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("なし", index < 0)) {
            index = -1;
            changed = true;
        }
        for (std::size_t i = 0; i < model.morphs.size(); ++i) {
            const bool selected = index == static_cast<std::int32_t>(i);
            const auto name = model.morphs[i].name.empty() ? "(無名)" : model.morphs[i].name.c_str();
            if (ImGui::Selectable(name, selected)) {
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

bool chooseMaterial(const char *label, const mmd::PmxModel &model, std::int32_t &index) {
    const std::string current = index >= 0 && static_cast<std::size_t>(index) < model.materials.size()
                                    ? model.materials[static_cast<std::size_t>(index)].name
                                    : "全材質 / なし";
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable("全材質", index == -1)) {
            index = -1;
            changed = true;
        }
        for (std::size_t i = 0; i < model.materials.size(); ++i) {
            const bool selected = index == static_cast<std::int32_t>(i);
            if (ImGui::Selectable(model.materials[i].name.c_str(), selected)) {
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

bool chooseRigidBody(const char *label, const mmd::PmxModel &model, std::int32_t &index) {
    const std::string current = index >= 0 && static_cast<std::size_t>(index) < model.rigidBodies.size()
                                    ? model.rigidBodies[static_cast<std::size_t>(index)].name
                                    : "なし";
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        for (std::size_t i = 0; i < model.rigidBodies.size(); ++i) {
            const bool selected = index == static_cast<std::int32_t>(i);
            if (ImGui::Selectable(model.rigidBodies[i].name.c_str(), selected)) {
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

bool chooseIndex(const char *label, std::size_t count, std::int32_t &index) {
    if (count == 0)
        return false;
    const auto old = index;
    int value = index;
    ImGui::InputInt(label, &value);
    value = std::clamp(value, 0, static_cast<int>(count - 1));
    index = value;
    return old != index;
}

std::optional<SelectionKind> toSelectionKind(mmd::ReferenceObjectKind kind) {
    switch (kind) {
    case mmd::ReferenceObjectKind::vertex:
        return SelectionKind::vertex;
    case mmd::ReferenceObjectKind::material:
        return SelectionKind::material;
    case mmd::ReferenceObjectKind::bone:
        return SelectionKind::bone;
    case mmd::ReferenceObjectKind::morph:
        return SelectionKind::morph;
    case mmd::ReferenceObjectKind::displayFrame:
        return SelectionKind::displayFrame;
    case mmd::ReferenceObjectKind::rigidBody:
        return SelectionKind::rigidBody;
    case mmd::ReferenceObjectKind::joint:
        return SelectionKind::joint;
    case mmd::ReferenceObjectKind::softBody:
        return SelectionKind::softBody;
    case mmd::ReferenceObjectKind::face:
        return SelectionKind::face;
    case mmd::ReferenceObjectKind::model:
        return std::nullopt;
    }
    return std::nullopt;
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
    if (ImGui::Button("回復保存")) {
        const auto saved = writeRecovery(session).success;
        if (saved)
            session.lastRecovery = std::chrono::steady_clock::now();
        session.ui.status = saved ? "回復情報を保存しました" : "回復保存に失敗しました";
    }
    inputString("追加するモデル", session.ui.mergePath);
    if (ImGui::Button("モデルを追加") && !session.ui.mergePath.empty()) {
        try {
            const auto other = mmd::pmx::load(session.ui.mergePath);
            MergeReport report;
            if (mergeAppend(session, other, &report)) {
                session.ui.status = "モデルを追加しました";
                if (!report.conflicts.empty())
                    session.ui.status += "（名前の衝突 " + std::to_string(report.conflicts.size()) + "件）";
            } else {
                session.ui.status = "モデル追加に失敗しました";
            }
        } catch (const std::exception &error) {
            session.ui.status = error.what();
        }
        ImGui::End();
        return;
    }
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
    if (ImGui::Begin("差分")) {
        for (const auto &line : formatDifferences(differences))
            ImGui::TextWrapped("%s", line.c_str());
        if (differences.differences.empty())
            ImGui::TextUnformatted("差分はありません");
    }
    ImGui::End();
    const auto recipes = inspectStandardBones(model);
    ImGui::Text("準標準骨格: %zu / 不足 %zu", recipes.available, recipes.missing);
    if (ImGui::Button("不足骨格を追加") && recipes.missing != 0) {
        session.ui.status = applyStandardBones(session, standardBoneRecipes()) ? "骨格を追加しました" : "骨格追加に失敗しました";
    }
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
    for (std::size_t channel = 0; channel < std::min<std::size_t>(model.metadata.additionalUvCount, 4); ++channel) {
        const auto label = "追加UV " + std::to_string(channel);
        ImGui::InputFloat4(label.c_str(), draft.additionalUv[channel].data());
    }
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
    ImGui::SameLine();
    if (ImGui::Button("BDEF2→SDEF")) {
        const auto report = convertBdef2ToSdef(session, {handle});
        session.ui.status = report.converted != 0 ? "SDEFへ変換しました" : "SDEFへ変換できませんでした";
        session.ui.vertexDraft.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("SDEF→BDEF2")) {
        const auto report = convertSdefToBdef2(session, {handle});
        session.ui.status = report.converted != 0 ? "BDEF2へ変換しました" : "BDEF2へ変換できませんでした";
        session.ui.vertexDraft.reset();
    }
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
    int sphereMode = draft.sphereMode;
    int toonMode = draft.toonMode;
    ImGui::InputInt("球モード", &sphereMode);
    ImGui::InputInt("トゥーンモード", &toonMode);
    draft.sphereMode = static_cast<std::uint8_t>(std::clamp(sphereMode, 0, 3));
    draft.toonMode = static_cast<std::uint8_t>(std::clamp(toonMode, 0, 1));
    inputString("メモ", draft.memo);
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
    else
        ImGui::InputFloat3("末端オフセット", draft.tailOffset.data());
    if ((draft.flags & 0x0300U) != 0) {
        chooseBone("付与元", model, draft.inheritParent);
        ImGui::InputFloat("付与率", &draft.inheritRatio);
    }
    ImGui::InputFloat3("固定軸", draft.fixedAxis.data());
    ImGui::InputFloat3("ローカルX軸", draft.localAxisX.data());
    ImGui::InputFloat3("ローカルZ軸", draft.localAxisZ.data());
    ImGui::InputInt("外部親キー", &draft.externalParentKey);
    if ((draft.flags & 0x0020U) != 0) {
        chooseBone("IK対象", model, draft.ikTarget);
        ImGui::InputInt("IK回数", &draft.ikLoopCount);
        ImGui::InputFloat("IK角度", &draft.ikLimitAngle);
        ImGui::Text("IKリンク: %zu", draft.ikLinks.size());
        if (!draft.ikLinks.empty()) {
            selectIndex("IKリンク番号", draft.ikLinks.size(), session.ui.boneIkLinkIndex);
            auto &link = draft.ikLinks[session.ui.boneIkLinkIndex];
            chooseBone("IKリンク先", model, link.bone, false);
            ImGui::Checkbox("可動範囲制限", &link.limited);
            if (link.limited) {
                ImGui::InputFloat3("IK下限", link.minimum.data());
                ImGui::InputFloat3("IK上限", link.maximum.data());
            }
        }
    }
    const bool hasIk = (draft.flags & 0x0020U) != 0;
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
            if ((draft.flags & 1U) == 0 && !transaction.setBoneTailOffset(handle, draft.tailOffset))
                return false;
            if ((draft.flags & 0x0300U) != 0 &&
                !transaction.setBoneInheritParent(handle, draft.inheritParent >= 0
                                                             ? std::optional{session.document.boneHandle(static_cast<std::size_t>(draft.inheritParent))}
                                                             : std::nullopt))
                return false;
            if (!transaction.setBoneInheritRatio(handle, draft.inheritRatio) ||
                !transaction.setBoneFixedAxis(handle, draft.fixedAxis) ||
                !transaction.setBoneLocalAxes(handle, draft.localAxisX, draft.localAxisZ) ||
                !transaction.setBoneExternalParentKey(handle, draft.externalParentKey))
                return false;
            if ((draft.flags & 0x0020U) != 0 &&
                !transaction.setBoneIkTarget(handle, draft.ikTarget >= 0
                                                       ? std::optional{session.document.boneHandle(static_cast<std::size_t>(draft.ikTarget))}
                                                       : std::nullopt))
                return false;
            if (!transaction.setBoneIkLimits(handle, draft.ikLoopCount, draft.ikLimitAngle))
                return false;
            for (std::size_t index = 0; index < draft.ikLinks.size(); ++index)
                if (!transaction.setBoneIkLink(handle, index, draft.ikLinks[index]))
                    return false;
            return true;
        }, "ボーンを更新");
        session.ui.status = result.success ? "ボーンを更新しました" : result.message;
        session.ui.boneDraft.reset();
    }
    if (hasIk) {
        if (ImGui::Button("IKリンク追加")) {
            const auto result = applyTransaction(session, [&](auto &transaction) {
                return transaction.addBoneIkLink(handle, mmd::PmxIkLink{});
            }, "IKリンクを追加");
            session.ui.status = result.success ? "IKリンクを追加しました" : result.message;
            session.ui.boneDraft.reset();
            ImGui::End();
            return;
        }
        if (!draft.ikLinks.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("IKリンク削除")) {
                const auto result = applyTransaction(session, [&](auto &transaction) {
                    return transaction.eraseBoneIkLink(handle, session.ui.boneIkLinkIndex);
                }, "IKリンクを削除");
                session.ui.status = result.success ? "IKリンクを削除しました" : result.message;
                session.ui.boneIkLinkIndex = 0;
                session.ui.boneDraft.reset();
                ImGui::End();
                return;
            }
        }
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
    if (selectIndex("番号", model.morphs.size(), session.ui.morphIndex)) {
        session.ui.morphDraft.reset();
        session.ui.morphOffsetIndex = 0;
        session.ui.morphOffsetDirty = false;
    }
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
    bool offsetDirty = false;
    if (!draft.offsets.empty()) {
        if (selectIndex("オフセット番号", draft.offsets.size(), session.ui.morphOffsetIndex))
            offsetDirty = true;
        auto &offset = draft.offsets[session.ui.morphOffsetIndex];
        switch (draft.type) {
        case 0:
            offsetDirty = chooseMorph("対象モーフ", model, offset.index) || offsetDirty;
            offsetDirty = ImGui::InputFloat("重み", &offset.scalar) || offsetDirty;
            break;
        case 1:
            offsetDirty = chooseIndex("対象頂点", model.vertices.size(), offset.index) || offsetDirty;
            offsetDirty = ImGui::InputFloat3("移動", offset.vector3.data()) || offsetDirty;
            break;
        case 2:
            offsetDirty = chooseBone("対象ボーン", model, offset.index, false) || offsetDirty;
            offsetDirty = ImGui::InputFloat3("移動", offset.vector3.data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("回転", offset.vector4.data()) || offsetDirty;
            break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            offsetDirty = chooseIndex("対象頂点", model.vertices.size(), offset.index) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("UV移動", offset.vector4.data()) || offsetDirty;
            break;
        case 8: {
            offsetDirty = chooseMaterial("対象材質", model, offset.index) || offsetDirty;
            int operation = offset.operation;
            offsetDirty = ImGui::InputInt("演算", &operation) || offsetDirty;
            offset.operation = static_cast<std::uint8_t>(std::clamp(operation, 0, 1));
            offsetDirty = ImGui::InputFloat4("拡散色", offset.materialVectors[0].data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("鏡面色", offset.materialVectors[1].data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("環境・輪郭", offset.materialVectors[2].data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("輪郭色", offset.materialVectors[3].data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("テクスチャ色", offset.materialVectors[4].data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("球色", offset.materialVectors[5].data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat4("トゥーン色", offset.materialVectors[6].data()) || offsetDirty;
            break;
        }
        case 9:
            offsetDirty = chooseMorph("対象モーフ", model, offset.index) || offsetDirty;
            offsetDirty = ImGui::InputFloat("重み", &offset.scalar) || offsetDirty;
            break;
        case 10:
            offsetDirty = chooseRigidBody("対象剛体", model, offset.index) || offsetDirty;
            offsetDirty = ImGui::InputFloat3("速度", offset.vector3.data()) || offsetDirty;
            offsetDirty = ImGui::InputFloat3("トルク", offset.tertiaryVector3.data()) || offsetDirty;
            offsetDirty = ImGui::Checkbox("ローカル", &offset.local) || offsetDirty;
            break;
        default:
            break;
        }
    }
    session.ui.morphOffsetDirty = session.ui.morphOffsetDirty || offsetDirty;
    if (ImGui::Button("適用")) {
        OperationResult result;
        if (session.ui.morphOffsetDirty && !draft.offsets.empty()) {
            const auto &offset = draft.offsets[session.ui.morphOffsetIndex];
            result = applyTransaction(session, [&](auto &transaction) {
                if (!transaction.setMorphName(handle, draft.name) || !transaction.setMorphEnglishName(handle, draft.englishName) ||
                    !transaction.setMorphPanel(handle, draft.panel) || !transaction.setMorphType(handle, draft.type))
                    return false;
                switch (draft.type) {
                case 0:
                    return offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.morphs.size() &&
                           transaction.setGroupMorphOffset(handle, session.ui.morphOffsetIndex,
                                                           session.document.morphHandle(static_cast<std::size_t>(offset.index)), offset.scalar);
                case 1:
                    return offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.vertices.size() &&
                           transaction.setVertexMorphOffset(handle, session.ui.morphOffsetIndex,
                                                            session.document.vertexHandle(static_cast<std::size_t>(offset.index)), offset.vector3);
                case 2:
                    return offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.bones.size() &&
                           transaction.setBoneMorphOffset(handle, session.ui.morphOffsetIndex,
                                                          session.document.boneHandle(static_cast<std::size_t>(offset.index)), offset.vector3,
                                                          offset.vector4);
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    return offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.vertices.size() &&
                           transaction.setUvMorphOffset(handle, session.ui.morphOffsetIndex,
                                                        session.document.vertexHandle(static_cast<std::size_t>(offset.index)),
                                                        static_cast<std::uint32_t>(draft.type - 3), offset.vector4);
                case 8:
                    return transaction.setMaterialMorphOffset(handle, session.ui.morphOffsetIndex,
                                                               offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.materials.size()
                                                                   ? std::optional{session.document.materialHandle(static_cast<std::size_t>(offset.index))}
                                                                   : std::nullopt,
                                                               offset.operation, offset.materialVectors);
                case 9:
                    return offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.morphs.size() &&
                           transaction.setFlipMorphOffset(handle, session.ui.morphOffsetIndex,
                                                          session.document.morphHandle(static_cast<std::size_t>(offset.index)), offset.scalar);
                case 10:
                    return offset.index >= 0 && static_cast<std::size_t>(offset.index) < model.rigidBodies.size() &&
                           transaction.setImpulseMorphOffset(handle, session.ui.morphOffsetIndex,
                                                             session.document.rigidBodyHandle(static_cast<std::size_t>(offset.index)),
                                                             offset.vector3, offset.tertiaryVector3, offset.local);
                default:
                    return false;
                }
            }, "モーフオフセットを更新");
        } else {
            result = editMorph(session, handle, draft);
        }
        session.ui.status = result.success ? "モーフを更新しました" : (result.message.empty() ? "モーフ更新に失敗しました" : result.message);
        session.ui.morphDraft.reset();
        session.ui.morphOffsetDirty = false;
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("オフセット削除") && !draft.offsets.empty()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.eraseMorphOffset(handle, session.ui.morphOffsetIndex);
        }, "モーフオフセットを削除");
        if (result.success) {
            session.ui.morphOffsetIndex = 0;
            session.ui.morphDraft.reset();
            session.ui.morphOffsetDirty = false;
            ImGui::End();
            return;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("オフセットを前へ") && session.ui.morphOffsetIndex > 0) {
        if (applyTransaction(session, [&](auto &transaction) {
                return transaction.moveMorphOffset(handle, session.ui.morphOffsetIndex, session.ui.morphOffsetIndex - 1);
            }, "モーフオフセットを前へ移動").success)
            --session.ui.morphOffsetIndex;
        session.ui.morphDraft.reset();
        session.ui.morphOffsetDirty = false;
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("オフセットを後へ") && !draft.offsets.empty() && session.ui.morphOffsetIndex + 1 < draft.offsets.size()) {
        if (applyTransaction(session, [&](auto &transaction) {
                return transaction.moveMorphOffset(handle, session.ui.morphOffsetIndex, session.ui.morphOffsetIndex + 1);
            }, "モーフオフセットを後へ移動").success)
            ++session.ui.morphOffsetIndex;
        session.ui.morphDraft.reset();
        session.ui.morphOffsetDirty = false;
        ImGui::End();
        return;
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
    if (selectIndex("番号", model.displayFrames.size(), session.ui.displayFrameIndex)) {
        session.ui.displayFrameDraft.reset();
        session.ui.displayItemIndex = 0;
    }
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
    bool itemChanged = false;
    if (!draft.items.empty()) {
        if (selectIndex("項目番号", draft.items.size(), session.ui.displayItemIndex))
            itemChanged = true;
        auto &item = draft.items[session.ui.displayItemIndex];
        if (item.bone)
            itemChanged = chooseBone("対象ボーン", model, item.index, false) || itemChanged;
        else
            itemChanged = chooseMorph("対象モーフ", model, item.index) || itemChanged;
    }
    if (ImGui::Button("適用")) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            if (!transaction.setDisplayFrameName(handle, draft.name) ||
                !transaction.setDisplayFrameEnglishName(handle, draft.englishName))
                return false;
            if (!itemChanged || draft.items.empty())
                return true;
            const auto &item = draft.items[session.ui.displayItemIndex];
            if (item.index < 0)
                return false;
            if (item.bone && static_cast<std::size_t>(item.index) < model.bones.size())
                return transaction.setDisplayFrameItem(handle, session.ui.displayItemIndex,
                                                       session.document.boneHandle(static_cast<std::size_t>(item.index)));
            if (!item.bone && static_cast<std::size_t>(item.index) < model.morphs.size())
                return transaction.setDisplayFrameItem(handle, session.ui.displayItemIndex,
                                                       session.document.morphHandle(static_cast<std::size_t>(item.index)));
            return false;
        }, "表示枠を更新");
        session.ui.status = result.success ? "表示枠を更新しました" : result.message;
        session.ui.displayFrameDraft.reset();
        ImGui::End();
        return;
    }
    if (ImGui::Button("項目削除") && !draft.items.empty()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.eraseDisplayFrameItem(handle, session.ui.displayItemIndex);
        }, "表示枠項目を削除");
        session.ui.status = result.success ? "表示枠項目を削除しました" : result.message;
        session.ui.displayItemIndex = 0;
        session.ui.displayFrameDraft.reset();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("項目を前へ") && session.ui.displayItemIndex > 0) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveDisplayFrameItem(handle, session.ui.displayItemIndex, session.ui.displayItemIndex - 1);
        }, "表示枠項目を前へ移動");
        session.ui.status = result.success ? "表示枠項目を移動しました" : result.message;
        --session.ui.displayItemIndex;
        session.ui.displayFrameDraft.reset();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("項目を後へ") && !draft.items.empty() && session.ui.displayItemIndex + 1 < draft.items.size()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.moveDisplayFrameItem(handle, session.ui.displayItemIndex, session.ui.displayItemIndex + 1);
        }, "表示枠項目を後へ移動");
        session.ui.status = result.success ? "表示枠項目を移動しました" : result.message;
        ++session.ui.displayItemIndex;
        session.ui.displayFrameDraft.reset();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("ボーン項目追加") && !model.bones.empty()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.addDisplayFrameItem(handle, session.document.boneHandle(0));
        }, "表示枠にボーンを追加");
        session.ui.status = result.success ? "表示枠項目を追加しました" : result.message;
        session.ui.displayFrameDraft.reset();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("モーフ項目追加") && !model.morphs.empty()) {
        const auto result = applyTransaction(session, [&](auto &transaction) {
            return transaction.addDisplayFrameItem(handle, session.document.morphHandle(0));
        }, "表示枠にモーフを追加");
        session.ui.status = result.success ? "表示枠項目を追加しました" : result.message;
        session.ui.displayFrameDraft.reset();
        ImGui::End();
        return;
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
            int shape = draft.shape;
            ImGui::InputInt("形状", &shape);
            draft.shape = static_cast<std::uint8_t>(std::clamp(shape, 0, 2));
            ImGui::InputFloat3("大きさ", draft.size.data());
            ImGui::InputFloat3("位置", draft.position.data());
            ImGui::InputFloat3("回転", draft.rotation.data());
            ImGui::InputFloat("質量", &draft.mass);
            ImGui::InputFloat("移動減衰", &draft.linearDamping);
            ImGui::InputFloat("回転減衰", &draft.angularDamping);
            ImGui::InputFloat("反発", &draft.restitution);
            ImGui::InputFloat("摩擦", &draft.friction);
            int group = draft.group;
            int collisionMask = draft.collisionMask;
            ImGui::InputInt("衝突グループ", &group);
            ImGui::InputInt("衝突マスク", &collisionMask);
            draft.group = static_cast<std::uint8_t>(std::clamp(group, 0, 15));
            draft.collisionMask = static_cast<std::uint16_t>(std::clamp(collisionMask, 0, 65535));
            int mode = draft.mode;
            ImGui::InputInt("モード", &mode);
            draft.mode = static_cast<std::uint8_t>(std::clamp(mode, 0, 2));
            if (ImGui::Button("剛体を適用")) {
                const auto result = applyTransaction(session, [&](auto &transaction) {
                    const auto bone = draft.bone >= 0 && static_cast<std::size_t>(draft.bone) < model.bones.size()
                                          ? std::optional{session.document.boneHandle(static_cast<std::size_t>(draft.bone))}
                                          : std::nullopt;
                    return transaction.setRigidBodyName(handle, draft.name) &&
                           transaction.setRigidBodyEnglishName(handle, draft.englishName) &&
                           transaction.setRigidBodyBone(handle, bone) &&
                           transaction.setRigidBodyShape(handle, draft.shape, draft.size) &&
                           transaction.setRigidBodyTransform(handle, draft.position, draft.rotation) &&
                           transaction.setRigidBodyPhysical(handle, draft.mass, draft.linearDamping,
                                                            draft.angularDamping, draft.restitution, draft.friction) &&
                           transaction.setRigidBodyCollision(handle, draft.group, draft.collisionMask) &&
                           transaction.setRigidBodyMode(handle, draft.mode);
                }, "剛体を更新");
                session.ui.status = result.success ? "剛体を更新しました" : result.message;
                session.ui.rigidBodyDraft.reset();
                ImGui::End();
                return;
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
            chooseRigidBody("剛体A", model, draft.bodyA);
            chooseRigidBody("剛体B", model, draft.bodyB);
            int type = draft.type;
            ImGui::InputInt("種類", &type);
            draft.type = static_cast<std::uint8_t>(std::clamp(type, 0, 5));
            ImGui::InputFloat3("移動下限", draft.translationMinimum.data());
            ImGui::InputFloat3("移動上限", draft.translationMaximum.data());
            ImGui::InputFloat3("回転下限", draft.rotationMinimum.data());
            ImGui::InputFloat3("回転上限", draft.rotationMaximum.data());
            ImGui::InputFloat3("位置", draft.position.data());
            ImGui::InputFloat3("回転", draft.rotation.data());
            ImGui::InputFloat3("移動ばね", draft.translationSpring.data());
            ImGui::InputFloat3("回転ばね", draft.rotationSpring.data());
            if (ImGui::Button("ジョイントを適用")) {
                const auto result = applyTransaction(session, [&](auto &transaction) {
                    if (draft.bodyA < 0 || draft.bodyB < 0 || static_cast<std::size_t>(draft.bodyA) >= model.rigidBodies.size() ||
                        static_cast<std::size_t>(draft.bodyB) >= model.rigidBodies.size())
                        return false;
                    return transaction.setJointName(handle, draft.name) &&
                           transaction.setJointEnglishName(handle, draft.englishName) &&
                           transaction.setJointType(handle, draft.type) &&
                           transaction.setJointBodies(handle,
                                                      session.document.rigidBodyHandle(static_cast<std::size_t>(draft.bodyA)),
                                                      session.document.rigidBodyHandle(static_cast<std::size_t>(draft.bodyB))) &&
                           transaction.setJointTransform(handle, draft.position, draft.rotation) &&
                           transaction.setJointLimits(handle, draft.translationMinimum, draft.translationMaximum,
                                                      draft.rotationMinimum, draft.rotationMaximum) &&
                           transaction.setJointSprings(handle, draft.translationSpring, draft.rotationSpring);
                }, "ジョイントを更新");
                session.ui.status = result.success ? "ジョイントを更新しました" : result.message;
                session.ui.jointDraft.reset();
                ImGui::End();
                return;
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
            int shape = draft.shape;
            int group = draft.group;
            int collisionMask = draft.collisionMask;
            int flags = draft.flags;
            ImGui::InputInt("形状", &shape);
            ImGui::InputInt("材質番号", &draft.material);
            ImGui::InputInt("衝突グループ", &group);
            ImGui::InputInt("衝突マスク", &collisionMask);
            ImGui::InputInt("フラグ", &flags);
            draft.shape = static_cast<std::uint8_t>(std::clamp(shape, 0, 1));
            draft.group = static_cast<std::uint8_t>(std::clamp(group, 0, 15));
            draft.collisionMask = static_cast<std::uint16_t>(std::clamp(collisionMask, 0, 65535));
            draft.flags = static_cast<std::uint8_t>(std::clamp(flags, 0, 255));
            ImGui::InputInt("曲げリンク距離", &draft.bendingLinkDistance);
            ImGui::InputInt("クラスタ数", &draft.clusterCount);
            ImGui::InputFloat("総質量", &draft.totalMass);
            ImGui::InputFloat("衝突余白", &draft.collisionMargin);
            ImGui::InputInt("空気力モデル", &draft.aeroModel);
            for (std::size_t i = 0; i < draft.config.size(); i += 3) {
                const auto label = "設定 " + std::to_string(i / 3);
                ImGui::InputFloat3(label.c_str(), draft.config.data() + i);
            }
            ImGui::InputFloat3("クラスタ設定", draft.cluster.data());
            ImGui::InputInt4("反復設定", draft.iteration.data());
            ImGui::InputFloat3("材質設定", draft.materialConfig.data());
            ImGui::Text("アンカー %zu / 固定頂点 %zu", draft.anchors.size(), draft.pinnedVertices.size());
            if (ImGui::Button("ソフトボディを適用")) {
                const auto result = applyTransaction(session, [&](auto &transaction) {
                    return transaction.setSoftBodyName(handle, draft.name) &&
                           transaction.setSoftBodyEnglishName(handle, draft.englishName) &&
                           transaction.setSoftBody(handle, draft);
                }, "ソフトボディを更新");
                session.ui.status = result.success ? "ソフトボディを更新しました" : result.message;
                session.ui.softBodyDraft.reset();
                ImGui::End();
                return;
            }
        }
    }
    if (ImGui::Button("全ボーンから剛体を生成") && !model.bones.empty()) {
        std::vector<mmd::BoneHandle> handles;
        handles.reserve(model.bones.size());
        for (std::size_t i = 0; i < model.bones.size(); ++i)
            handles.push_back(session.document.boneHandle(i));
        session.ui.status = generateRigidBodyChain(session, handles, 1) ? "剛体を生成しました" : "剛体生成に失敗しました";
    }
    ImGui::End();
}

void drawDiagnosticsPanel(DocumentSession &session) {
    const auto detailed = validateForEditing(session.document.model());
    ImGui::Begin("診断");
    for (std::size_t index = 0; index < detailed.issues.size(); ++index) {
        const auto &issue = detailed.issues[index];
        const char *level = issue.severity >= mmd::ValidationSeverity::error ? "ERROR" : "WARN";
        ImGui::TextWrapped("[%s] %s: %s", level, issue.object.c_str(), issue.message.c_str());
        const auto selectable = toSelectionKind(issue.location.kind);
        if (selectable && issue.location.id != 0) {
            ImGui::SameLine();
            const auto button = "選択##診断" + std::to_string(index);
            if (ImGui::SmallButton(button.c_str()))
                session.selection.set({*selectable, issue.location.id, issue.location.generation});
        }
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
    if (session.modified && !session.path.empty()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - session.lastRecovery >= std::chrono::seconds(30) && writeRecovery(session).success)
            session.lastRecovery = now;
    }
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
