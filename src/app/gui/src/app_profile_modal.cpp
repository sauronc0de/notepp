#include "app.hpp"

#include "lang.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <SDL.h>
#include <imgui.h>

namespace
{
static ImVec2 pm_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}
} // namespace

void App::show_profile_modal()
{
  auto &m = profile_modal_;
  if(!m.open) return;

  // Centre the modal on first use
  const ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);

  const char *modal_title = m.copy_mode ? "Copy Profile###profile_modal"
                                        : (m.edit_idx >= 0 ? "Edit Profile###profile_modal"
                                                           : "New Profile###profile_modal");
  ImGui::OpenPopup("###profile_modal");

  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.12f, 0.17f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.00f, 0.00f, 0.00f, 0.55f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));

  if(ImGui::BeginPopupModal(modal_title, nullptr,
                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
  {
    // On first frame, initialise draft state
    if(m.first_frame)
    {
      m.first_frame = false;
      if(m.edit_idx >= 0 && m.edit_idx < (int)layout_profiles_.size())
      {
        const auto &src = layout_profiles_[(size_t)m.edit_idx];
        std::strncpy(m.name_buf, src.name.c_str(), sizeof(m.name_buf) - 1);
        if(m.copy_mode)
        {
          std::string copy_name = std::string(src.name) + " (copy)";
          std::strncpy(m.name_buf, copy_name.c_str(), sizeof(m.name_buf) - 1);
        }
        m.maximized = src.window_maximized;
        m.pos_x = src.window_x;
        m.pos_y = src.window_y;
        m.size_w = src.window_w;
        m.size_h = src.window_h;
      }
      else
      {
        std::strncpy(m.name_buf, "Profile", sizeof(m.name_buf) - 1);
        m.maximized = false;
        m.pos_x = 100;
        m.pos_y = 100;
        m.size_w = 1100;
        m.size_h = 700;
      }
      m.dragging_win = false;
      m.resizing_win = false;
    }

    // ---- Name ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
    ImGui::TextUnformatted(Lang::t("Profile name:"));
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##pm_name", m.name_buf, sizeof(m.name_buf));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Maximized checkbox ----
    ImGui::Checkbox(Lang::t("Maximized"), &m.maximized);

    ImGui::Spacing();

    // ---- Visual window picker (only when not maximized) ----
    if(!m.maximized)
    {
      // Compute canvas and scale
      SDL_Rect disp = {0, 0, 1920, 1080};
      SDL_GetDisplayBounds(0, &disp);
      const float canvas_w = ImGui::GetContentRegionAvail().x;
      const float canvas_h = canvas_w * (float)disp.h / (float)disp.w;
      const float scale = canvas_w / (float)disp.w;

      const ImVec2 canvas_tl = ImGui::GetCursorScreenPos();
      const ImVec2 canvas_br(canvas_tl.x + canvas_w, canvas_tl.y + canvas_h);

      // Canvas background = monitor
      ImDrawList *dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(canvas_tl, canvas_br, IM_COL32(12, 14, 20, 255), 6.0f);
      dl->AddRect(canvas_tl, canvas_br, IM_COL32(50, 70, 110, 200), 6.0f, 0, 1.5f);

      // Clamp draft window position so it fits in monitor
      m.pos_x = std::max(0, std::min(m.pos_x, disp.w - m.size_w));
      m.pos_y = std::max(0, std::min(m.pos_y, disp.h - m.size_h));
      m.size_w = std::max(320, std::min(m.size_w, disp.w));
      m.size_h = std::max(200, std::min(m.size_h, disp.h));

      // Window rect in canvas space
      const float wx0 = canvas_tl.x + m.pos_x * scale;
      const float wy0 = canvas_tl.y + m.pos_y * scale;
      const float wx1 = wx0 + m.size_w * scale;
      const float wy1 = wy0 + m.size_h * scale;
      const ImVec2 wpos(wx0, wy0), wsize(wx1, wy1);

      // Draw window: body gradient-like fill
      dl->AddRectFilled(wpos, wsize, IM_COL32(22, 50, 100, 210), 3.0f);
      // Title bar stripe
      const float tb_h = std::max(4.0f, 20.0f * scale);
      dl->AddRectFilled(wpos, ImVec2(wx1, wy0 + tb_h), IM_COL32(60, 120, 230, 230), 3.0f,
                        ImDrawFlags_RoundCornersTop);
      // Border
      dl->AddRect(wpos, wsize, IM_COL32(80, 150, 255, 200), 3.0f, 0, 1.2f);
      // Resize handle (bottom-right triangle)
      dl->AddTriangleFilled(ImVec2(wx1, wy1 - 10.0f), ImVec2(wx1 - 10.0f, wy1),
                            ImVec2(wx1, wy1), IM_COL32(100, 170, 255, 180));

      // Invisible button covering canvas for drag/resize interaction
      ImGui::SetCursorScreenPos(canvas_tl);
      ImGui::InvisibleButton("##pm_canvas", pm_nonzero_invisible_button_size(canvas_w, canvas_h));
      const bool canvas_active = ImGui::IsItemActive();
      const bool canvas_hovered = ImGui::IsItemHovered();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      // Determine hover zones (in canvas coords)
      const bool over_win = (mouse.x >= wx0 && mouse.x < wx1 &&
                             mouse.y >= wy0 && mouse.y < wy1);
      const bool over_resize = (mouse.x >= wx1 - 14.0f && mouse.y >= wy1 - 14.0f &&
                                mouse.x < wx1 && mouse.y < wy1);

      if(canvas_active)
      {
        if(!m.dragging_win && !m.resizing_win)
        {
          // Start drag or resize
          if(over_resize)
            m.resizing_win = true;
          else if(over_win)
          {
            m.dragging_win = true;
            m.drag_offset_x = (int)((mouse.x - canvas_tl.x) / scale) - m.pos_x;
            m.drag_offset_y = (int)((mouse.y - canvas_tl.y) / scale) - m.pos_y;
          }
        }
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if(m.dragging_win)
        {
          m.pos_x = (int)((mouse.x - canvas_tl.x) / scale) - m.drag_offset_x;
          m.pos_y = (int)((mouse.y - canvas_tl.y) / scale) - m.drag_offset_y;
        }
        else if(m.resizing_win)
        {
          m.size_w = std::max(320, m.size_w + (int)(delta.x / scale));
          m.size_h = std::max(200, m.size_h + (int)(delta.y / scale));
        }
      }
      else
      {
        m.dragging_win = false;
        m.resizing_win = false;
      }

      // Cursor hint
      if(canvas_hovered && over_resize)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
      else if(canvas_hovered && over_win)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

      ImGui::Spacing();

      // Numeric inputs
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.9f, 1.0f));
      ImGui::TextUnformatted(Lang::t("Position"));
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.0f);
      ImGui::InputInt("X##pm_x", &m.pos_x, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.0f);
      ImGui::InputInt("Y##pm_y", &m.pos_y, 0);

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.9f, 1.0f));
      ImGui::TextUnformatted(Lang::t("Size"));
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.0f);
      ImGui::InputInt("W##pm_w", &m.size_w, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.0f);
      ImGui::InputInt("H##pm_h", &m.size_h, 0);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Buttons ----
    const float btn_w = 110.0f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btn_w * 2.0f - ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().WindowPadding.x);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.30f, 1.0f));
    if(ImGui::Button(Lang::t("Cancel"), ImVec2(btn_w, 0)))
    {
      ImGui::CloseCurrentPopup();
      m.open = false;
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    const bool name_ok = m.name_buf[0] != '\0';
    ImGui::BeginDisabled(!name_ok);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 1.00f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.80f, 1.0f));
    const char *ok_label = (m.edit_idx >= 0 && !m.copy_mode) ? Lang::t("Save Profile") : Lang::t("Create Profile");
    if(ImGui::Button(ok_label, ImVec2(btn_w, 0)))
    {
      const std::string pname(m.name_buf);
      if(m.edit_idx >= 0 && !m.copy_mode)
      {
        // Edit existing
        push_profile_snapshot();
        auto &ep = layout_profiles_[(size_t)m.edit_idx];
        ep.name = pname;
        const bool was_maximized = ep.window_maximized;
        ep.window_maximized = m.maximized;
        ep.window_x = m.pos_x;
        ep.window_y = m.pos_y;
        ep.window_w = m.size_w;
        ep.window_h = m.size_h;
        if(ep.id == active_profile_id_)
        {
          // Re-apply window change if editing the active profile
          apply_profile(ep, true);
          window_profile_check_pending_ = false;
          window_profile_check_delay_ = 20;
        }
        (void)was_maximized;
        save_profiles();
        save_index();
      }
      else
      {
        // Create or copy
        create_profile(pname, m.maximized, m.pos_x, m.pos_y, m.size_w, m.size_h);
      }
      ImGui::CloseCurrentPopup();
      m.open = false;
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::EndPopup();
  }
  else
  {
    // Popup was closed externally (e.g., Escape)
    m.open = false;
  }

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}
