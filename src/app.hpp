#pragma once

#include "undo_redo.hpp"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct SDL_Window;

struct AppConfig
{
  std::filesystem::path assetsPath;
  std::filesystem::path dataPath;
};

class App
{
public:
  explicit App(AppConfig config);
  int run();

private:
  AppConfig config_;
  void init_sdl_gl();
  void init_imgui();
  void load_state();
  void save_state();
  void save_index() const;
  void load_note_content_for_active();
  void set_active_note(int folder_idx, int note_idx);
  void ensure_default_index();
  void sync_project_files();
  /**
   * @brief Clamp active folder/note indices to valid ranges.
   *
   * Keeps at least one folder, but allows that folder to have zero notes.
   * In that case active_note_idx_ is set to -1.
   */
  void normalize_active_indices();
  /**
   * @brief Returns true when active_folder_idx_/active_note_idx_ point to an existing note.
   */
  bool has_active_note() const;
  std::string make_unique_note_title(int folder_idx, const std::string &base_title, int ignore_note_idx = -1) const;
  std::string make_note_path(const std::string &folder_name, const std::string &note_title) const;
  void sync_active_note_meta();
  void rename_note_storage_for_title(const std::string &new_title);
  void rename_note_by_index(int folder_idx, int note_idx, const std::string &new_title);
  void push_undo_snapshot_from(const std::string &snapshot);
  void push_undo_snapshot();
  void apply_undo_snapshot();
  void apply_redo_snapshot();
  std::string capture_workspace_snapshot() const;
  void apply_workspace_snapshot(std::string_view snapshot);
  std::string capture_text_context_snapshot() const;
  void apply_text_history_state(std::string_view note_path, std::string_view text, std::string_view context_snapshot);
  void apply_preview_history_state(std::string_view note_path, std::string_view text, std::string_view preview_state_snapshot);
  void record_workspace_history_action(std::string_view label, std::string before_snapshot);
  void record_text_history_action(std::string_view label, const std::string &before_text, const std::string &after_text);
  void record_preview_history_action(std::string_view label, std::string_view note_path, const std::string &before_text, const std::string &after_text, const std::string &before_preview_state, const std::string &after_preview_state);
  void update_pending_text_history(std::string_view label, const std::string &before_text, const std::string &after_text, bool start_new_chunk);
  void flush_pending_text_history();
  void discard_pending_text_history();
  bool apply_global_undo();
  bool apply_global_redo();
  bool find_note_by_path(std::string_view path, int &folder_idx, int &note_idx) const;
  std::string make_history_debug_context(std::string_view preferred_note_path = {}) const;
  void show_history_indicator(std::string_view prefix, std::string_view label, ImVec4 accent);
  void render_history_indicator() const;
  void render_debug_history_window() const;
  void shutdown();

  void frame_begin();
  void frame_ui();
  void frame_end();
  void save_note_clipboard();
#if USE_PORTABLE_PATHS
  void switch_project(const std::filesystem::path &new_root);
#endif

  std::filesystem::path default_state_file_;
  std::filesystem::path legacy_state_meta_file_;
  std::filesystem::path index_file_;
  std::filesystem::path imgui_ini_file_;
  std::filesystem::path drawings_file_;
  std::filesystem::path g_clipboard_file;

  SDL_Window *window_ = nullptr;
  void *gl_context_ = nullptr;
  ImFont *font_regular_ = nullptr;
  ImFont *font_italic_ = nullptr;
  ImFont *font_bold_ = nullptr;
  std::string default_font_path_;

  bool running_ = true;
  bool editing_mode_ = false;
  bool request_exit_edit_mode_ = false;
  bool request_clear_selection_ = false;
  bool request_cancel_draw_tools_ = false;
  bool request_rename_selected_ = false;
  bool request_delete_selected_ = false;
  bool request_undo_draw_ = false;
  bool request_redo_draw_ = false;
  bool request_undo_edit_ = false;
  bool request_redo_edit_ = false;
  bool request_copy_sidebar_ = false;
  bool request_paste_sidebar_ = false;
  bool request_open_search_ = false;
  bool request_open_project_search_ = false;
#if USE_PORTABLE_PATHS
  bool request_open_project_ = false;
#endif
  bool request_close_search_ = false;
  bool search_window_visible_ = false;
  bool search_request_window_focus_ = false;
  bool layout_locked_ = false;
  bool detached_note_windows_enabled_ = true;
  bool history_replay_in_progress_ = false;
  bool force_note_layout_restore_ = false;
  std::string search_jump_note_path_;
  int search_jump_pos_ = -1;
  int search_jump_len_ = 0;
  bool search_jump_force_edit_ = false;
  std::string note_title_ = "Note";
  std::string state_file_path_;
  struct NoteMeta
  {
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
  };
  struct FolderMeta
  {
    std::string name;
    std::vector<NoteMeta> notes;
  };
  struct PendingDroppedFile
  {
    std::string path;
    int mouse_x = 0;
    int mouse_y = 0;
  };

  std::vector<FolderMeta> folders_;
  std::vector<PendingDroppedFile> pending_dropped_files_;
  std::vector<std::string> pending_fs_delete_paths_;
  int active_folder_idx_ = 0;
  int active_note_idx_ = 0;
  bool folder_overview_mode_ = false;
  mutable bool layout_dirty_ = false;
  UndoRedo::HistoryManager history_;
  std::unordered_set<unsigned int> pinned_topmost_viewports_;

  struct PendingTextHistory
  {
    bool active = false;
    std::string label;
    std::string note_path;
    std::string before_text;
    std::string after_text;
    std::string context_snapshot;
  };
  PendingTextHistory pending_text_history_;
  std::string deferred_text_snapshot_before_;

  struct HistoryIndicator
  {
    std::string text;
    ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
    double until = 0.0;
  };
  HistoryIndicator history_indicator_;

  std::string markdown_text_ =
      "# Notes (Markdown preview)\n"
      "\n"
      "This is a **minimal** ImGui app using `enkisoftware/imgui_markdown`.\n"
      "\n"
      "- Lists\n"
      "- `inline code`\n"
      "- [Links](https://github.com/enkisoftware/imgui_markdown)\n"
      "\n"
      "```cpp\n"
      "int main(){ return 0; }\n"
      "```\n";
};
