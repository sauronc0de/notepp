#pragma once

#include <string_view>

#include <imgui.h>

namespace NoteUi
{
struct NoteTheme
{
  ImVec4 window_bg;
  ImVec4 title_bg;
  ImVec4 title_bg_active;
  ImVec4 title_bg_collapsed;
  ImVec4 border;
};

ImVec4 mix_color(ImVec4 a, ImVec4 b, float t);
ImVec4 with_alpha(ImVec4 c, float a);
ImVec4 folder_accent_color(bool use_custom_color, float color_r, float color_g, float color_b, const ImGuiStyle &style);
NoteTheme make_note_theme(bool use_custom_color, float color_r, float color_g, float color_b, const ImGuiStyle &style);
int push_folder_imgui_theme(const NoteTheme &nt, const ImGuiStyle &style);
ImTextureID get_toolbar_icon_texture(std::string_view icon_name);
void clear_toolbar_icon_cache();
} // namespace NoteUi
