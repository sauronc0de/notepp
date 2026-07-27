#pragma once

#include "emoji_picker.hpp"
#include "note_history.hpp"
#include "note_model.hpp"
#include "note_storage.hpp"
#include "terminal.hpp"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SDL_Window;

struct AppConfig
{
  std::filesystem::path assetsPath;
  std::filesystem::path dataPath;
  std::filesystem::path configPath;
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
  void save_index();
  void sync_active_folder_settings();
  void apply_folder_settings(int folder_idx);
  void load_note_content_for_active();
  const std::string &cached_note_text(const std::string &path);
  void update_note_cache(const std::string &path, std::string text);
  void invalidate_note_cache(const std::string &path);
  void set_active_note(int folder_idx, int note_idx);
  void ensure_default_index();
  void open_or_create_readme();
  bool sync_project_files();
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

  using NoteLayoutData = notepp::note_model::NoteLayoutData;
  using LayoutProfile = notepp::note_model::LayoutProfile;
  struct ProfileModalState
  {
    bool open = false;
    bool first_frame = false; // true on the frame the modal is opened
    int edit_idx = -1;        // -1 = create new, >=0 = edit/copy existing
    bool copy_mode = false;   // true when opening as a copy
    char name_buf[64] = {};
    bool maximized = true;
    int pos_x = 100, pos_y = 100;
    int size_w = 1100, size_h = 700;
    // For the visual picker drag state
    bool dragging_win = false;
    bool resizing_win = false;
    int drag_offset_x = 0, drag_offset_y = 0;
  };

  void load_profiles();
  void save_profiles();
  void capture_to_active_profile();
  void apply_profile(const LayoutProfile &profile, bool apply_window_state = true);
  std::string create_profile(const std::string &name, bool maximized, int x, int y, int w, int h);
  void delete_profile(const std::string &id);
  LayoutProfile *find_active_profile();
  const LayoutProfile *find_matching_profile() const;
  void do_window_profile_switch();
  bool is_window_covering_display() const;
  void push_profile_snapshot();
  void show_profile_modal();

  bool frame_begin();
  void frame_ui();
  void frame_end();
  void configure_frame_limiter(bool software_gl);
  void limit_frame_rate();
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
  std::filesystem::path profiles_file_;

  SDL_Window *window_ = nullptr;
  void *gl_context_ = nullptr;
  ImFont *font_regular_ = nullptr;
  ImFont *font_italic_ = nullptr;
  ImFont *font_bold_ = nullptr;
  ImFont *font_terminal_ = nullptr;
  std::string default_font_path_;
  int max_fps_ = 60;
  unsigned int file_watch_timer_ = 0;
  bool dirty_ = true;
  notepp::note_storage::NoteContentCache note_content_cache_;
  unsigned long long min_frame_ticks_ = 0;
  unsigned long long last_frame_ticks_ = 0;

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
  bool request_select_line_ = false;
  bool request_copy_sidebar_ = false;
  bool request_paste_sidebar_ = false;
  bool show_emoji_picker_ = false;
  std::string pending_emoji_insert_;
  EmojiPicker emoji_picker_;

  bool request_open_search_ = false;
  bool request_open_project_search_ = false;
  bool request_open_terminal_ = false;
#if USE_PORTABLE_PATHS
  bool request_open_project_ = false;
#endif
  bool reset_sidebar_state_ = false;
  bool request_close_search_ = false;
  bool search_window_visible_ = false;
  bool search_request_window_focus_ = false;
  bool layout_locked_ = false;
  bool detached_note_windows_enabled_ = false;
  bool dockers_enabled_ = false;
  bool drawings_visible_ = true;
  bool grid_visible_ = false;
  bool history_replay_in_progress_ = false;
  bool force_note_layout_restore_ = false;
  std::string search_jump_note_path_;
  int search_jump_pos_ = -1;
  int search_jump_len_ = 0;
  bool search_jump_force_edit_ = false;
  std::string note_title_ = "Note";
  std::string state_file_path_;
  using NoteMeta = notepp::note_model::NoteMeta;
  using FolderMeta = notepp::note_model::FolderMeta;
  struct PendingDroppedFile
  {
    std::string path;
    int mouse_x = 0;
    int mouse_y = 0;
  };

  std::vector<FolderMeta> folders_;
  std::vector<LayoutProfile> layout_profiles_;
  std::string active_profile_id_;
  std::string maximized_profile_id_;
  std::string reduced_profile_id_;
  bool window_profile_check_pending_ = false;
  int window_profile_check_delay_ = 0;
  ProfileModalState profile_modal_;
  bool manage_profiles_open_ = false;
  // Borderless window drag
  bool window_drag_active_ = false;
  bool window_drag_was_maximized_ = false;
  int window_drag_start_mx_ = 0, window_drag_start_my_ = 0;
  int window_drag_start_wx_ = 0, window_drag_start_wy_ = 0;
  std::vector<PendingDroppedFile> pending_dropped_files_;
  std::vector<std::string> pending_fs_delete_paths_;
  Terminal terminal_;
  std::string pending_terminal_text_;
  bool terminal_visible_ = false;
  int active_folder_idx_ = 0;
  int active_note_idx_ = 0;
  bool folder_overview_mode_ = false;
  mutable bool layout_dirty_ = false;
  bool state_dirty_ = false;
  NoteHistory::HistoryManager history_;
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
