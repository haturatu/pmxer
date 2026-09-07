#include "OutlinerPanel.hpp"

#include "../editor/DocumentSession.hpp"
#include "EditorPanels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <limits>
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
  SelectionKind kind{};
};

void applySelection(ImGuiSelectionExternalStorage *storage, int index,
                    bool selected) {
  auto &context = *static_cast<MultiSelectContext *>(storage->UserData);
  if (index < 0)
    return;
  const auto item =
      itemAt(*context.session, context.kind, static_cast<std::size_t>(index));
  if (selected)
    context.session->selection.add(item);
  else
    context.session->selection.remove(item);
}

template <typename Label>
void drawMultiSelectList(DocumentSession &session, SelectionKind kind,
                         std::size_t count, Label label,
                         std::string_view filter, float height = 180.0F) {
  if (!ImGui::BeginChild("##items", ImVec2(0.0F, height),
                         ImGuiChildFlags_Borders)) {
    ImGui::EndChild();
    return;
  }
  MultiSelectContext context{&session, kind};
  ImGuiSelectionExternalStorage storage;
  storage.UserData = &context;
  storage.AdapterSetItemSelected = applySelection;
  const auto flags =
      ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
  auto *selection = ImGui::BeginMultiSelect(
      flags, itemCount(selectedCount(session, kind)), itemCount(count));
  storage.ApplyRequests(selection);
  ImGuiListClipper clipper;
  clipper.Begin(itemCount(count));
  if (selection->RangeSrcItem != -1)
    clipper.IncludeItemByIndex(static_cast<int>(selection->RangeSrcItem));
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const auto index = static_cast<std::size_t>(row);
      const auto text = label(index);
      if (!matches(text, filter))
        continue;
      const auto item = itemAt(session, kind, index);
      ImGui::SetNextItemSelectionUserData(row);
      if (ImGui::Selectable((text + "##" + std::to_string(row)).c_str(),
                            session.selection.contains(item)))
        activate(session, kind, index);
    }
  }
  selection = ImGui::EndMultiSelect();
  storage.ApplyRequests(selection);
  ImGui::EndChild();
}

void drawBoneNode(DocumentSession &session,
                  const std::vector<std::vector<std::size_t>> &children,
                  std::size_t index, std::string_view filter,
                  std::vector<bool> &visited) {
  if (index >= children.size() || visited[index])
    return;
  visited[index] = true;
  const auto &bone = session.document.model().bones[index];
  const auto visible =
      matches(bone.name, filter) || matches(bone.englishName, filter);
  const auto item = itemAt(session, SelectionKind::bone, index);
  ImGui::PushID(static_cast<int>(index));
  auto flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (children[index].empty())
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (session.selection.contains(item))
    flags |= ImGuiTreeNodeFlags_Selected;
  const auto open = ImGui::TreeNodeEx(visible ? bone.name.c_str() : "…", flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    if (ImGui::GetIO().KeyCtrl) {
      if (session.selection.contains(item))
        session.selection.remove(item);
      else
        session.selection.add(item);
    } else {
      session.selection.set(item);
    }
    activate(session, SelectionKind::bone, index);
  }
  if (open && !children[index].empty()) {
    for (const auto child : children[index])
      drawBoneNode(session, children, child, filter, visited);
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void drawBoneTree(DocumentSession &session, std::string_view filter) {
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
  for (const auto root : roots)
    drawBoneNode(session, children, root, filter, visited);
  for (std::size_t index = 0; index < bones.size(); ++index)
    if (!visited[index])
      drawBoneNode(session, children, index, filter, visited);
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
  ImGui::SetNextItemWidth(-1.0F);
  ImGui::InputTextWithHint("##search", "検索", workspace.search.data(),
                           workspace.search.size());
  const std::string_view filter(workspace.search.data());
  const auto &model = session.document.model();

  if (ImGui::CollapsingHeader("材質", ImGuiTreeNodeFlags_DefaultOpen))
    drawMultiSelectList(
        session, SelectionKind::material, model.materials.size(),
        [&](std::size_t i) { return indexedName(model.materials[i].name, i); },
        filter);
  if (ImGui::CollapsingHeader("ボーン", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginChild("##bone-tree", ImVec2(0.0F, 260.0F),
                          ImGuiChildFlags_Borders))
      drawBoneTree(session, filter);
    ImGui::EndChild();
  }
  if (ImGui::CollapsingHeader("モーフ"))
    drawMultiSelectList(
        session, SelectionKind::morph, model.morphs.size(),
        [&](std::size_t i) { return indexedName(model.morphs[i].name, i); },
        filter);
  if (ImGui::CollapsingHeader("頂点"))
    drawMultiSelectList(
        session, SelectionKind::vertex, model.vertices.size(),
        [](std::size_t i) { return "頂点 " + std::to_string(i); }, filter,
        260.0F);
  if (ImGui::CollapsingHeader("テクスチャ"))
    drawMultiSelectList(
        session, SelectionKind::texture, model.textures.size(),
        [&](std::size_t i) {
          return indexedName(model.textures[i].storedPath, i);
        },
        filter);
  if (ImGui::CollapsingHeader("剛体"))
    drawMultiSelectList(
        session, SelectionKind::rigidBody, model.rigidBodies.size(),
        [&](std::size_t i) {
          return indexedName(model.rigidBodies[i].name, i);
        },
        filter);
  if (ImGui::CollapsingHeader("ジョイント"))
    drawMultiSelectList(
        session, SelectionKind::joint, model.joints.size(),
        [&](std::size_t i) { return indexedName(model.joints[i].name, i); },
        filter);
  ImGui::End();
}

} // namespace pmxer
