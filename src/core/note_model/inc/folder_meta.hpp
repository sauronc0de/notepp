#pragma once

#include "note_meta.hpp"

#include <string>
#include <vector>

namespace notepp::note_model
{
struct FolderMeta
{
  std::string name;
  std::vector<NoteMeta> notes;
  std::vector<std::string> images; // tracked image file paths (abs)
  bool layout_locked = false;
  bool detached_note_windows = false;
  bool dockers_enabled = false;
  bool drawings_visible = true;
  bool grid_visible = false;
};
} // namespace notepp::note_model