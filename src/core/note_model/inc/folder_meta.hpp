#pragma once

#include "note_meta.hpp"

#include <string>
#include <utility>
#include <vector>

namespace notepp::note_model
{
struct FolderMeta
{
  FolderMeta() = default;

  FolderMeta(std::string folder_name, std::vector<NoteMeta> folder_notes)
      : name(std::move(folder_name)),
        notes(std::move(folder_notes))
  {
  }

  std::string name;
  std::vector<NoteMeta> notes;
  std::vector<std::string> images;                   // resolved runtime image paths
  std::vector<std::string> unresolved_stored_images; // retained metadata, never used for I/O
  bool layout_locked = false;
  bool detached_note_windows = false;
  bool dockers_enabled = false;
  bool drawings_visible = true;
  bool grid_visible = false;
};
} // namespace notepp::note_model
