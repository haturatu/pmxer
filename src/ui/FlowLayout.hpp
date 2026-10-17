#pragma once

#include <imgui.h>
#include <algorithm>

namespace pmxer::ui {

// Keep translated controls on the same line only when their full label fits.
// The default reserves space for a radio button or checkbox as well as its text.
inline void sameLineIfFits(const char *label, float controlWidth = 0.0F) {
    const auto &style = ImGui::GetStyle();
    const auto width = controlWidth > 0.0F ? controlWidth
        : ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetFrameHeight() +
              style.ItemInnerSpacing.x + style.FramePadding.x * 2.0F;
    const auto right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
        ImGui::SameLine();
}

inline void fitInputWidth(const char *label) {
    const auto labelWidth = ImGui::CalcTextSize(label, nullptr, true).x;
    ImGui::SetNextItemWidth(std::max(60.0F, ImGui::GetContentRegionAvail().x -
        labelWidth - ImGui::GetStyle().ItemInnerSpacing.x));
}

} // namespace pmxer::ui
