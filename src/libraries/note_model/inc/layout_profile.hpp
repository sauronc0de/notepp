#pragma once

#include "note_meta.hpp"

#include <imgui.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace notepp::note_model
{
struct NoteLayoutData
{
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  float width = 520.0f;
  float height = 260.0f;
  bool hidden = false;
  bool has_layout = false;
  ImGuiID dock_id = 0;
};

struct LayoutProfile
{
  std::string id;
  std::string name;
  bool window_maximized = true;
  int window_x = 100, window_y = 100;
  int window_w = 1100, window_h = 700;
  bool pending_delete = false;
  std::unordered_map<std::string, NoteLayoutData> note_layouts;
};
} // namespace notepp::note_model