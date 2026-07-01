#include "app.hpp"

#include "note_ui.hpp"

#include <imgui.h>

using StringUtils::clamp01f;
using NoteUi::mix_color;

void App::show_history_indicator(std::string_view prefix, std::string_view label, ImVec4 accent)
{
  history_indicator_.text.assign(prefix.begin(), prefix.end());
  if(!label.empty())
  {
    history_indicator_.text += ": ";
    history_indicator_.text.append(label.begin(), label.end());
  }
  history_indicator_.accent = accent;
  history_indicator_.until = ImGui::GetTime() + 1.15;
}

void App::render_history_indicator() const
{
  if(history_indicator_.text.empty()) return;

  const double remaining = history_indicator_.until - ImGui::GetTime();
  if(remaining <= 0.0) return;

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  if(viewport == nullptr) return;

  const float fade = clamp01f(static_cast<float>(remaining / 1.15));
  const float alpha = 0.18f + 0.82f * fade;
  const ImVec2 pad(10.0f, 7.0f);
  const ImVec2 text_size = ImGui::CalcTextSize(history_indicator_.text.c_str());
  const ImVec2 size(text_size.x + pad.x * 2.0f, text_size.y + pad.y * 2.0f);
  const ImVec2 pos(
      viewport->Pos.x + viewport->Size.x * 0.5f - size.x * 0.5f,
      viewport->Pos.y + viewport->Size.y - size.y - 22.0f);
  const ImVec2 max(pos.x + size.x, pos.y + size.y);

  ImDrawList *fg = ImGui::GetForegroundDrawList(viewport);
  const ImVec4 bg(0.08f, 0.09f, 0.11f, 0.86f * alpha);
  const ImVec4 border = mix_color(ImVec4(0.42f, 0.45f, 0.50f, 1.0f), history_indicator_.accent, 0.55f);
  const ImVec4 text = mix_color(ImVec4(0.92f, 0.93f, 0.95f, 1.0f), history_indicator_.accent, 0.18f);

  fg->AddRectFilled(pos, max, ImGui::GetColorU32(bg), 8.0f);
  fg->AddRect(pos, max, ImGui::GetColorU32(ImVec4(border.x, border.y, border.z, 0.62f * alpha)), 8.0f, 0, 1.0f);
  fg->AddText(
      ImVec2(pos.x + pad.x, pos.y + pad.y),
      ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.96f * alpha)),
      history_indicator_.text.c_str());
}