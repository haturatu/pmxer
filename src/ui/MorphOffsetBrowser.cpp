#include "MorphOffsetBrowser.hpp"

#include "../editor/DocumentSession.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace pmxer {
namespace {

std::string indexedName(std::string_view name, std::size_t index) {
    return name.empty() ? "#" + std::to_string(index) : std::string(name);
}

std::string offsetLabel(const DocumentSession &session,
                        const mmd::PmxMorph &morph, std::size_t index) {
    const auto target = morph.offsets[index].index;
    const auto &model = session.document.model();
    if ((morph.type == 0U || morph.type == 9U) && target >= 0 &&
        static_cast<std::size_t>(target) < model.morphs.size())
        return "モーフ " + indexedName(
                               model.morphs[static_cast<std::size_t>(target)].name,
                               static_cast<std::size_t>(target));
    if (morph.type == 2U && target >= 0 &&
        static_cast<std::size_t>(target) < model.bones.size())
        return "ボーン " + indexedName(
                               model.bones[static_cast<std::size_t>(target)].name,
                               static_cast<std::size_t>(target));
    if (morph.type == 8U && target < 0)
        return "材質 全材質";
    if (morph.type == 8U && static_cast<std::size_t>(target) < model.materials.size())
        return "材質 " + indexedName(
                               model.materials[static_cast<std::size_t>(target)].name,
                               static_cast<std::size_t>(target));
    if (morph.type == 10U && target >= 0 &&
        static_cast<std::size_t>(target) < model.rigidBodies.size())
        return "剛体 " + indexedName(
                               model.rigidBodies[static_cast<std::size_t>(target)].name,
                               static_cast<std::size_t>(target));
    return (morph.type == 2U ? "ボーン " : "頂点 ") +
           std::to_string(target);
}

bool matches(std::string_view label, std::string_view query,
             std::size_t index) {
    return query.empty() || label.find(query) != std::string_view::npos ||
           std::to_string(index).find(query) != std::string_view::npos;
}

} // namespace

MorphOffsetBrowserResult drawMorphOffsetBrowser(
    DocumentSession &session, const mmd::PmxMorph &morph,
    std::size_t &selectedIndex, std::string_view id,
    mmd::MorphHandle morphHandle) {
    MorphOffsetBrowserResult result;
    if (morph.offsets.empty())
        return result;
    selectedIndex = std::min(selectedIndex, morph.offsets.size() - 1U);
    ImGui::InputTextWithHint(
        ("対象検索##morph-offset-search-" + std::string(id)).c_str(),
        "名前または番号", session.ui.morphOffsetSearch.data(),
        session.ui.morphOffsetSearch.size());
    const std::string_view query(session.ui.morphOffsetSearch.data());
    const bool filtering = !query.empty();
    if (filtering) {
        auto &cache = session.ui.morphOffsetFilter;
        if (cache.morphId != morphHandle.id ||
            cache.morphGeneration != morphHandle.generation ||
            cache.documentRevision != session.revision || cache.query != query) {
            cache.morphId = morphHandle.id;
            cache.morphGeneration = morphHandle.generation;
            cache.documentRevision = session.revision;
            cache.query = query;
            cache.visible.clear();
            cache.visible.reserve(morph.offsets.size());
            for (std::size_t index = 0; index < morph.offsets.size(); ++index)
                if (matches(offsetLabel(session, morph, index), query, index))
                    cache.visible.push_back(index);
        }
    }
    const auto childId = "##morph-offset-browser-" + std::string(id);
    if (ImGui::BeginChild(childId.c_str(), ImVec2(0.0F, 180.0F),
                          ImGuiChildFlags_Borders)) {
        const auto count = filtering ? session.ui.morphOffsetFilter.visible.size()
                                     : morph.offsets.size();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(count));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto displayIndex = static_cast<std::size_t>(row);
                const auto index = filtering
                                       ? session.ui.morphOffsetFilter.visible[displayIndex]
                                       : displayIndex;
                const auto label = offsetLabel(session, morph, index) +
                                   "##" + std::string(id) + "-offset-" +
                                   std::to_string(index);
                if (ImGui::Selectable(label.c_str(), selectedIndex == index)) {
                    selectedIndex = index;
                    result.selectionChanged = true;
                }
            }
        }
    }
    ImGui::EndChild();
    return result;
}

} // namespace pmxer
