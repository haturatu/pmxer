#include "OutlinerPanel.hpp"

#include "../editor/DocumentSession.hpp"
#include "../editor/EditorSelectionController.hpp"
#include "../editor/WorkspacePolicy.hpp"
#include "EditorPanels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmxer {
namespace {

int itemCount(std::size_t count) {
  return static_cast<int>(std::min(
      count, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

SelectionItem itemAt(const DocumentSession &session, SelectionKind kind,
                     std::size_t index) {
  const auto make = [kind](const auto &handle) {
    return SelectionItem{kind, handle.domain, handle.id, handle.generation};
  };
  switch (kind) {
  case SelectionKind::vertex:
    return make(session.document.vertexHandle(index));
  case SelectionKind::texture:
    return make(session.document.textureHandle(index));
  case SelectionKind::material:
    return make(session.document.materialHandle(index));
  case SelectionKind::bone:
    return make(session.document.boneHandle(index));
  case SelectionKind::morph:
    return make(session.document.morphHandle(index));
  case SelectionKind::displayFrame:
    return make(session.document.displayFrameHandle(index));
  case SelectionKind::rigidBody:
    return make(session.document.rigidBodyHandle(index));
  case SelectionKind::joint:
    return make(session.document.jointHandle(index));
  case SelectionKind::softBody:
    return make(session.document.softBodyHandle(index));
  case SelectionKind::face:
    return make(session.document.faceHandle(index));
  }
  return {};
}

void activate(DocumentSession &session, SelectionKind kind, std::size_t index) {
  session.ui.clearDrafts();
  switch (kind) {
  case SelectionKind::vertex:
    session.ui.vertexIndex = index;
    break;
  case SelectionKind::texture:
    session.ui.textureIndex = index;
    break;
  case SelectionKind::material:
    session.ui.materialIndex = index;
    break;
  case SelectionKind::bone:
    session.ui.boneIndex = index;
    break;
  case SelectionKind::morph:
    session.ui.morphIndex = index;
    break;
  case SelectionKind::displayFrame:
    session.ui.displayFrameIndex = index;
    break;
  case SelectionKind::rigidBody:
    session.ui.rigidBodyIndex = index;
    break;
  case SelectionKind::joint:
    session.ui.jointIndex = index;
    break;
  case SelectionKind::softBody:
    session.ui.softBodyIndex = index;
    break;
  case SelectionKind::face:
    break;
  }
}

bool matches(std::string_view value, std::string_view filter) {
  if (filter.empty())
    return true;
  std::string foldedValue(value);
  std::string foldedFilter(filter);
  const auto fold = [](char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<char>(character - 'A' + 'a')
               : character;
  };
  std::transform(foldedValue.begin(), foldedValue.end(), foldedValue.begin(),
                 fold);
  std::transform(foldedFilter.begin(), foldedFilter.end(), foldedFilter.begin(),
                 fold);
  return foldedValue.find(foldedFilter) != std::string::npos;
}

std::size_t selectedCount(const DocumentSession &session, SelectionKind kind) {
  return static_cast<std::size_t>(std::count_if(
      session.selection.items().begin(), session.selection.items().end(),
      [kind](const auto &item) { return item.kind == kind; }));
}

struct MultiSelectContext {
  DocumentSession *session{};
  WorkspaceUiState *workspace{};
  SelectionKind kind{};
  const std::vector<std::size_t> *visible{};
};

void applySelection(ImGuiSelectionExternalStorage *storage, int index,
                    bool selected) {
  auto &context = *static_cast<MultiSelectContext *>(storage->UserData);
  if (index < 0)
    return;
  const auto row = static_cast<std::size_t>(index);
  if (context.visible != nullptr && row >= context.visible->size())
    return;
  const auto itemIndex = context.visible == nullptr
                             ? row
                             : (*context.visible)[row];
  const auto item = itemAt(*context.session, context.kind, itemIndex);
  if (selected)
    addSelection(*context.session, context.workspace->active, item,
                 SelectionOrigin::outliner);
  else
    context.session->selection.remove(item);
}

template <typename Label>
void drawMultiSelectList(DocumentSession &session, SelectionKind kind,
                         std::size_t count, Label label,
                         std::string_view filter, WorkspaceUiState &workspace,
                         bool &revealBlockedByFilter,
                         float height = 180.0F) {
  if (!ImGui::BeginChild("##items", ImVec2(0.0F, height),
                         ImGuiChildFlags_Borders)) {
    ImGui::EndChild();
    return;
  }
  std::vector<std::size_t> visible;
  if (!filter.empty()) {
    visible.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
      if (matches(label(index), filter))
        visible.push_back(index);
  }
  const auto *mapping = filter.empty() ? nullptr : &visible;
  const auto displayedCount = mapping == nullptr ? count : mapping->size();
  std::optional<int> pendingRow;
  if (session.ui.pendingOutlinerReveal &&
      session.ui.pendingOutlinerReveal->kind == kind) {
    for (std::size_t row = 0; row < displayedCount; ++row) {
      const auto index = mapping == nullptr ? row : (*mapping)[row];
      if (itemAt(session, kind, index) == *session.ui.pendingOutlinerReveal) {
        pendingRow = static_cast<int>(row);
        break;
      }
    }
    if (!pendingRow && !filter.empty())
      revealBlockedByFilter = true;
    else if (!pendingRow && filter.empty())
      session.ui.pendingOutlinerReveal.reset();
  }
  MultiSelectContext context{&session, &workspace, kind, mapping};
  ImGuiSelectionExternalStorage storage;
  storage.UserData = &context;
  storage.AdapterSetItemSelected = applySelection;
  const auto flags =
      ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
  auto *selection = ImGui::BeginMultiSelect(
      flags, itemCount(selectedCount(session, kind)), itemCount(displayedCount));
  storage.ApplyRequests(selection);
  ImGuiListClipper clipper;
  clipper.Begin(itemCount(displayedCount));
  if (selection->RangeSrcItem != -1)
    clipper.IncludeItemByIndex(static_cast<int>(selection->RangeSrcItem));
  if (pendingRow)
    clipper.IncludeItemByIndex(*pendingRow);
  bool revealed = false;
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const auto displayIndex = static_cast<std::size_t>(row);
      const auto index = mapping == nullptr ? displayIndex
                                            : (*mapping)[displayIndex];
      const auto text = label(index);
      const auto item = itemAt(session, kind, index);
      ImGui::SetNextItemSelectionUserData(row);
      if (ImGui::Selectable((text + "##" + std::to_string(row)).c_str(),
                            session.selection.contains(item)))
        activate(session, kind, index);
      if (pendingRow && row == *pendingRow) {
        ImGui::SetScrollHereY(0.5F);
        revealed = true;
      }
    }
  }
  selection = ImGui::EndMultiSelect();
  storage.ApplyRequests(selection);
  ImGui::EndChild();
  if (revealed)
    session.ui.pendingOutlinerReveal.reset();
}

struct BoneFilterResult {
  std::vector<bool> visible;
  std::vector<bool> matched;
  std::vector<bool> forceOpen;
};

BoneFilterResult makeBoneFilterResult(const std::vector<mmd::PmxBone> &bones,
                                      std::string_view filter) {
  BoneFilterResult result{std::vector<bool>(bones.size()),
                          std::vector<bool>(bones.size()),
                          std::vector<bool>(bones.size())};
  for (std::size_t index = 0; index < bones.size(); ++index) {
    result.matched[index] = matches(bones[index].name, filter) ||
                            matches(bones[index].englishName, filter);
    result.visible[index] = result.matched[index];
  }
  for (std::size_t index = 0; index < bones.size(); ++index) {
    if (!result.matched[index])
      continue;
    auto current = index;
    for (std::size_t steps = 0; steps < bones.size(); ++steps) {
      const auto parent = bones[current].parent;
      if (parent < 0 || static_cast<std::size_t>(parent) >= bones.size() ||
          static_cast<std::size_t>(parent) == current)
        break;
      const auto parentIndex = static_cast<std::size_t>(parent);
      result.visible[parentIndex] = true;
      result.forceOpen[parentIndex] = true;
      current = parentIndex;
    }
  }
  if (filter.empty())
    std::fill(result.visible.begin(), result.visible.end(), true);
  return result;
}

void drawBoneNode(DocumentSession &session, WorkspaceUiState &workspace,
                  const std::vector<std::vector<std::size_t>> &children,
                  std::size_t index, const BoneFilterResult &filter,
                  std::vector<bool> &visited,
                  const std::optional<SelectionItem> &pending,
                  bool &revealed) {
  if (index >= children.size() || visited[index] || !filter.visible[index])
    return;
  visited[index] = true;
  const auto &bone = session.document.model().bones[index];
  const auto item = itemAt(session, SelectionKind::bone, index);
  ImGui::PushID(static_cast<int>(index));
  auto flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (children[index].empty())
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (session.selection.contains(item))
    flags |= ImGuiTreeNodeFlags_Selected;
  if (filter.forceOpen[index])
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  const auto open = ImGui::TreeNodeEx(bone.name.c_str(), flags);
  if (pending && *pending == item) {
    ImGui::SetScrollHereY(0.5F);
    revealed = true;
  }
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    if (ImGui::GetIO().KeyCtrl) {
      if (session.selection.contains(item))
        session.selection.remove(item);
      else
        addSelection(session, workspace.active, item, SelectionOrigin::outliner);
    } else {
      selectPrimary(session, workspace.active, item, SelectionOrigin::outliner);
    }
    activate(session, SelectionKind::bone, index);
  }
  if (open && !children[index].empty()) {
    for (const auto child : children[index])
      drawBoneNode(session, workspace, children, child, filter, visited,
                   pending, revealed);
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void drawBoneTree(DocumentSession &session, WorkspaceUiState &workspace,
                  std::string_view filter, bool &revealBlockedByFilter) {
  const auto &bones = session.document.model().bones;
  std::vector<std::vector<std::size_t>> children(bones.size());
  std::vector<std::size_t> roots;
  for (std::size_t index = 0; index < bones.size(); ++index) {
    const auto parent = bones[index].parent;
    if (parent >= 0 && static_cast<std::size_t>(parent) < bones.size() &&
        static_cast<std::size_t>(parent) != index)
      children[static_cast<std::size_t>(parent)].push_back(index);
    else
      roots.push_back(index);
  }
  std::vector<bool> visited(bones.size());
  auto filterResult = makeBoneFilterResult(bones, filter);
  bool revealed = false;
  if (session.ui.pendingOutlinerReveal &&
      session.ui.pendingOutlinerReveal->kind == SelectionKind::bone) {
    std::optional<std::size_t> target;
    for (std::size_t index = 0; index < bones.size(); ++index)
      if (itemAt(session, SelectionKind::bone, index) ==
          *session.ui.pendingOutlinerReveal) {
        target = index;
        break;
      }
    if (target && filterResult.visible[*target]) {
      auto current = *target;
      for (std::size_t steps = 0; steps < bones.size(); ++steps) {
        const auto parent = bones[current].parent;
        if (parent < 0 || static_cast<std::size_t>(parent) >= bones.size())
          break;
        const auto parentIndex = static_cast<std::size_t>(parent);
        filterResult.forceOpen[parentIndex] = true;
        current = parentIndex;
      }
    } else if (target && !filter.empty()) {
      revealBlockedByFilter = true;
    } else if (!target && filter.empty()) {
      session.ui.pendingOutlinerReveal.reset();
    }
  }
  for (const auto root : roots)
    drawBoneNode(session, workspace, children, root, filterResult, visited,
                 session.ui.pendingOutlinerReveal, revealed);
  for (std::size_t index = 0; index < bones.size(); ++index)
    if (!visited[index])
      drawBoneNode(session, workspace, children, index, filterResult, visited,
                   session.ui.pendingOutlinerReveal, revealed);
  if (revealed)
    session.ui.pendingOutlinerReveal.reset();
}

bool pendingRevealFor(const DocumentSession &session,
                      SelectionKind kind) noexcept {
  return session.ui.pendingOutlinerReveal.has_value() &&
         session.ui.pendingOutlinerReveal->kind == kind;
}

std::string indexedName(std::string_view name, std::size_t index) {
  return name.empty() ? "#" + std::to_string(index) : std::string(name);
}

} // namespace

void drawOutlinerPanel(DocumentSession &session, WorkspaceUiState &workspace,
                       bool *open) {
  if (!ImGui::Begin("アウトライナー", open)) {
    ImGui::End();
    return;
  }
  auto &query = session.ui.outliner[workspaceIndex(workspace.active)].query;
  const auto clearWidth = ImGui::CalcTextSize("×").x +
                          ImGui::GetStyle().FramePadding.x * 2.0F;
  const auto spacing = ImGui::GetStyle().ItemSpacing.x;
  ImGui::SetNextItemWidth(std::max(
      80.0F, ImGui::GetContentRegionAvail().x - clearWidth - spacing));
  ImGui::InputTextWithHint("##search", "検索", query.data(), query.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("×##clear-outliner-search"))
    query.fill('\0');
  const std::string_view filter(query.data());
  const auto &model = session.document.model();

  const auto policy = workspacePolicy(workspace.active);
  const auto showModel = policy.allows(SelectionKind::material) ||
                         policy.allows(SelectionKind::texture);
  const auto showRig = policy.allows(SelectionKind::vertex) ||
                       policy.allows(SelectionKind::bone);
  const auto showMorph = policy.allows(SelectionKind::morph);
  const auto showPhysics = policy.allows(SelectionKind::rigidBody) ||
                           policy.allows(SelectionKind::joint) ||
                           policy.allows(SelectionKind::softBody);
  bool revealBlockedByFilter = false;

  if (showModel && pendingRevealFor(session, SelectionKind::material))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showModel &&
      ImGui::CollapsingHeader("材質", ImGuiTreeNodeFlags_DefaultOpen))
    drawMultiSelectList(
        session, SelectionKind::material, model.materials.size(),
        [&](std::size_t i) { return indexedName(model.materials[i].name, i); },
        filter, workspace, revealBlockedByFilter);
  if (showRig && pendingRevealFor(session, SelectionKind::bone))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showRig &&
      ImGui::CollapsingHeader("ボーン", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginChild("##bone-tree", ImVec2(0.0F, 260.0F),
                          ImGuiChildFlags_Borders))
      drawBoneTree(session, workspace, filter, revealBlockedByFilter);
    ImGui::EndChild();
  }
  if (showMorph && pendingRevealFor(session, SelectionKind::morph))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showMorph &&
      ImGui::CollapsingHeader("モーフ", ImGuiTreeNodeFlags_DefaultOpen))
    drawMultiSelectList(
        session, SelectionKind::morph, model.morphs.size(),
        [&](std::size_t i) { return indexedName(model.morphs[i].name, i); },
        filter, workspace, revealBlockedByFilter);
  if (showRig && pendingRevealFor(session, SelectionKind::vertex))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showRig && ImGui::CollapsingHeader("頂点"))
    drawMultiSelectList(
        session, SelectionKind::vertex, model.vertices.size(),
        [](std::size_t i) { return "頂点 " + std::to_string(i); }, filter,
        workspace, revealBlockedByFilter, 260.0F);
  if (showModel && pendingRevealFor(session, SelectionKind::texture))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showModel && ImGui::CollapsingHeader("テクスチャ"))
    drawMultiSelectList(
        session, SelectionKind::texture, model.textures.size(),
        [&](std::size_t i) {
          return indexedName(model.textures[i].storedPath, i);
        },
        filter, workspace, revealBlockedByFilter);
  if (showPhysics && pendingRevealFor(session, SelectionKind::rigidBody))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showPhysics &&
      ImGui::CollapsingHeader("剛体", ImGuiTreeNodeFlags_DefaultOpen))
    drawMultiSelectList(
        session, SelectionKind::rigidBody, model.rigidBodies.size(),
        [&](std::size_t i) {
          return indexedName(model.rigidBodies[i].name, i);
        },
        filter, workspace, revealBlockedByFilter);
  if (showPhysics && pendingRevealFor(session, SelectionKind::joint))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showPhysics && ImGui::CollapsingHeader("ジョイント"))
    drawMultiSelectList(
        session, SelectionKind::joint, model.joints.size(),
        [&](std::size_t i) { return indexedName(model.joints[i].name, i); },
        filter, workspace, revealBlockedByFilter);
  if (showPhysics && pendingRevealFor(session, SelectionKind::softBody))
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (showPhysics && ImGui::CollapsingHeader("ソフトボディ"))
    drawMultiSelectList(
        session, SelectionKind::softBody, model.softBodies.size(),
        [&](std::size_t i) {
          return indexedName(model.softBodies[i].name, i);
        },
        filter, workspace, revealBlockedByFilter);
  if (revealBlockedByFilter) {
    ImGui::Separator();
    ImGui::TextDisabled("選択中の項目は検索条件で非表示です");
    if (ImGui::SmallButton("検索をクリア##outliner-reveal"))
      query.fill('\0');
  }
  ImGui::End();
}

} // namespace pmxer
