#pragma once

#include <imgui.h>

#include <string>

namespace notepp::note_model
{
struct NoteMeta
{
  std::string id;
  std::string title;
  std::string path;
  std::string font_path;
  float font_size = 0.0f;
  bool use_custom_color = false;
  float color_r = 0.0f;
  float color_g = 0.0f;
  float color_b = 0.0f;
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  float width = 520.0f;
  float height = 260.0f;
  bool has_layout = false;
  bool hidden = false;
  bool always_on_top = false;
  ImGuiID dock_id = 0;
};
} // namespace notepp::note_model