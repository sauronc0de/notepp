#pragma once

#include "app_settings.hpp"
#include "atomic_file.hpp"
#include "command_api.hpp"
#include "command_ipc.hpp"
#include "emoji_picker.hpp"
#include "git_sync.hpp"
#include "note_history.hpp"
#include "note_diff.hpp"
#include "note_model.hpp"
#include "note_storage.hpp"
#include "project_settings.hpp"
#include "terminal.hpp"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SDL_Window;

struct AppConfig
{
  std::filesystem::path assetsPath;
  std::filesystem::path appSettingsPath;
  std::filesystem::path projectRoot;
  std::filesystem::path dataPath;
  std::filesystem::path configPath;
  std::filesystem::path workspacePath;
  bool hasInitialGitStatus = false;
  notepp::git_sync::Status initialGitStatus;
};

class App
{
public:
  explicit App(AppConfig config);
  int run();

private:
  AppConfig config_;
  notepp::app_settings::Store app_settings_store_;
  notepp::project_settings::Store project_settings_store_;
  process::SystemRunner git_process_runner_;
  notepp::git_sync::Client git_client_;
  bool git_sync_enabled_ = false;
  bool project_language_explicit_ = false;
  bool project_settings_dirty_ = false;
  enum class GitOperationKind
  {
    none,
    inspect,
    manual,
    project_switch,
    close
  };
  struct GitAsyncResult
  {
    notepp::git_sync::OperationResult operation;
    std::filesystem::path switch_root;
  };
  bool git_status_available_ = false;
  bool git_sync_in_progress_ = false;
  GitOperationKind git_operation_kind_ = GitOperationKind::none;
  bool close_requested_ = false;
  bool close_persisted_ = false;
  bool close_save_succeeded_ = false;
  notepp::git_sync::Status git_status_;
  std::string git_last_attempt_;
  std::future<GitAsyncResult> git_sync_future_;
  std::string app_settings_error_;
  std::string project_settings_error_;
  void record_git_status(const notepp::git_sync::Status &status);
  void begin_git_operation(bool manual_sync);
  void begin_close_git_operation();
  void finish_git_operation();
  void request_close();
  void advance_close();
  bool save_imgui_settings();
  bool save_project_settings();
  bool persist_before_close();
  void destroy_runtime();
  void reload_project_after_git_pull();
#if USE_PORTABLE_PATHS
  void load_switched_project(const std::filesystem::path &new_root);
#endif
  void init_sdl_gl();
  void init_imgui();
  void load_state();
  bool save_state(bool reset_edit_checkpoint = false);
  bool save_index();
  void sync_active_folder_settings();
  void apply_folder_settings(int folder_idx);
  void load_note_content_for_active(bool preserve_edit_checkpoint = false);
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
  void apply_global_variable_history_state(const std::filesystem::path &path,
                                           std::string_view expected,
                                           std::string_view replacement);
  void record_global_variable_history_action(const std::filesystem::path &path,
                                             std::string before,
                                             std::string after);
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
  void invalidate_note_comparison();
  void capture_edit_checkpoint(std::string_view before_text);
  void capture_edit_checkpoint_for_path(std::string_view path, std::string_view before_text);
  void migrate_edit_checkpoint(std::string_view old_path, std::string_view new_path);
  void begin_note_comparison();
  void start_note_comparison_git_request();
  void render_note_comparison();
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
  bool save_profiles();
  void capture_to_active_profile();
  void apply_profile(const LayoutProfile &profile, bool apply_window_state = true,
                     bool mark_dirty = true);
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
  void render_variable_inspector();
  void frame_end();
  void configure_frame_limiter(bool software_gl);
  void limit_frame_rate();
  bool save_note_clipboard();
  bool save_workspace();
  void load_workspace();
  bool save_workspace_text(const std::filesystem::path &path, std::string_view content);
  void configure_workspace_paths();
  void rebind_command_ipc();
  void post_command_mutation(const nlohmann::json &request,
                             const notepp::command_api::Response &response);
  void flush_pending_command_mutations();
  void migrate_legacy_workspace_files();
  bool load_project_settings();
#if USE_PORTABLE_PATHS
  bool switch_project(const std::filesystem::path &new_root);
#endif

  std::filesystem::path default_state_file_;
  std::filesystem::path legacy_state_meta_file_;
  std::filesystem::path index_file_;
  std::filesystem::path imgui_ini_file_;
  std::filesystem::path drawings_file_;
  std::filesystem::path g_clipboard_file;
  std::filesystem::path profiles_file_;
  std::filesystem::path workspace_file_;
  std::string index_source_document_;
  std::string profiles_source_document_;
  bool index_writable_ = true;
  bool profiles_writable_ = true;
  bool workspace_writable_ = true;
  bool workspace_migration_complete_ = false;
  bool legacy_workspace_migration_complete_ = true;

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
  std::unique_ptr<notepp::command_api::Api> command_api_;
  notepp::command_ipc::Server command_ipc_server_;
  struct PendingCommandMutation
  {
    nlohmann::json request;
    notepp::command_api::Response response;
  };
  std::vector<PendingCommandMutation> pending_command_mutations_;
  bool frame_ui_active_ = false;
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

  enum class EditorAction
  {
    none,
    task_list,
    table,
    bold,
    italic,
    strikethrough,
    quote,
    color,
    create_reference,
    mermaid_demo,
    ui_block
  };

  enum class UiWidgetChooserState
  {
    closed,
    open_requested,
    open
  };

  bool request_open_search_ = false;
  bool request_open_project_search_ = false;
  bool request_open_note_switcher_ = false;
  bool note_header_create_selecting_ = false;
  bool note_reference_selecting_ = false;
  bool note_reference_insertion_pending_ = false;
  int note_reference_selection_start_ = -1;
  int note_reference_selection_end_ = -1;
  std::string note_reference_target_;
  std::string note_reference_label_;
  bool note_reference_label_visible_ = false;
  bool note_reference_label_focus_ = false;
  char note_reference_label_buffer_[512] = {};
  std::filesystem::path pending_internal_link_path_;
  std::string pending_internal_link_anchor_;
  bool note_header_create_name_visible_ = false;
  bool request_note_header_create_confirm_ = false;
  bool request_close_note_header_create_ = false;
  bool note_header_create_focus_input_ = false;
  char note_header_create_title_[256] = {};
  std::string note_header_create_note_reference_;
  std::string note_header_create_parent_;
  std::string note_header_create_parent_path_;
  std::size_t note_header_create_parent_occurrence_ = 0;
  bool note_line_create_selecting_ = false;
  bool note_line_create_visible_ = false;
  bool request_note_line_create_confirm_ = false;
  bool request_close_note_line_create_ = false;
  bool note_line_create_focus_input_ = false;
  char note_line_create_line_[2048] = {};
  std::string note_line_create_note_reference_;
  std::string note_line_create_heading_;
  std::string note_line_create_heading_path_;
  std::size_t note_line_create_heading_occurrence_ = 0;
  bool note_color_set_selecting_ = false;
  bool note_color_set_visible_ = false;
  bool request_note_color_set_confirm_ = false;
  bool request_close_note_color_set_ = false;
  bool note_color_set_focus_input_ = false;
  float note_color_set_color_[3] = {0.35f, 0.65f, 1.0f};
  int note_color_set_selected_preset_ = 3;
  int note_color_set_navigation_delta_ = 0;
  std::string note_color_set_note_reference_;
  bool request_open_terminal_ = false;
  bool request_open_command_finder_ = false;
  bool request_close_command_finder_ = false;
  bool request_activate_command_finder_ = false;
  int command_finder_navigation_delta_ = 0;
  bool command_finder_window_visible_ = false;
  std::string command_finder_feedback_;
  bool variable_inspector_visible_ = false;
  bool variable_inspector_refresh_requested_ = false;
  bool request_open_editor_actions_ = false;
  bool request_close_editor_actions_ = false;
  bool request_activate_editor_actions_ = false;
  int editor_actions_navigation_delta_ = 0;
  bool editor_actions_window_visible_ = false;
  EditorAction request_editor_action_ = EditorAction::none;
  bool editor_action_selection_available_ = false;
  UiWidgetChooserState editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
  bool request_close_editor_action_ui_widget_chooser_ = false;
  bool editor_action_mermaid_chooser_requested_ = false;
  bool editor_action_mermaid_chooser_visible_ = false;
  bool request_close_editor_action_mermaid_chooser_ = false;
  std::string preview_edit_cursor_note_path_;
  int preview_edit_cursor_pos_ = -1;
  bool request_new_note_ = false;
  bool request_hide_focused_note_ = false;
  bool request_hide_folder_notes_ = false;
  int request_cycle_visible_notes_ = 0;
  bool request_focus_active_note_ = false;
  bool request_focus_editor_ = false;
  bool request_focus_sidebar_ = false;
  bool request_navigation_back_ = false;
  bool request_navigation_forward_ = false;
#if USE_PORTABLE_PATHS
  bool request_open_project_ = false;
#endif
  bool reset_sidebar_state_ = false;
  bool request_close_search_ = false;
  bool request_close_note_switcher_ = false;
  bool request_search_activate_ = false;
  bool request_note_switcher_activate_ = false;
  bool request_note_switcher_edit_ = false;
  bool request_edit_selected_note_ = false;
  int search_navigation_delta_ = 0;
  int search_selected_idx_ = -1;
  int note_switcher_navigation_delta_ = 0;
  int note_switcher_selected_idx_ = -1;
  bool search_window_visible_ = false;
  bool note_switcher_window_visible_ = false;
  bool search_request_window_focus_ = false;
  bool layout_locked_ = false;
  bool detached_note_windows_enabled_ = false;
  bool detach_transition_pending_ = false;
  bool detach_transition_target_ = false;
  bool dockers_enabled_ = false;
  bool drawings_visible_ = true;
  bool grid_visible_ = false;
  bool history_replay_in_progress_ = false;
  bool force_note_layout_restore_ = false;
  std::string search_jump_note_path_;
  int search_jump_pos_ = -1;
  int search_jump_len_ = 0;
  bool search_jump_force_edit_ = false;
  std::string search_editor_scroll_note_path_;
  int search_editor_scroll_pos_ = -1;
  std::string search_editor_selection_note_path_;
  int search_editor_selection_start_ = -1;
  int search_editor_selection_end_ = -1;
  bool search_editor_selection_focus_pending_ = false;
  std::string search_editor_match_note_path_;
  int search_editor_match_offset_ = -1;
  int search_editor_match_length_ = 0;
  std::string note_title_ = "Note";
  std::string state_file_path_;
  bool active_note_read_failed_ = false;
  std::string active_note_read_error_;
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
  bool suppress_terminal_text_input_ = false;
  bool terminal_visible_ = false;
  bool terminal_new_tab_on_open_ = false;
  float terminal_height_ = 360.0F;
  int active_folder_idx_ = 0;
  int active_note_idx_ = 0;
  bool folder_overview_mode_ = false;
  mutable bool layout_dirty_ = false;
  bool state_dirty_ = false;
  bool explicit_save_requested_ = false;
  bool last_save_succeeded_ = false;
  int index_schema_version_ = 3;
  bool index_paths_portable_ = true;
  NoteHistory::HistoryManager history_;
  NoteHistory::NavigationHistory navigation_history_;
  std::optional<NoteHistory::NavigationLocation> observed_navigation_location_;
  bool navigation_replay_in_progress_ = false;
  int navigation_editor_cursor_ = -1;
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

  enum class ComparisonBaseline
  {
    edit_checkpoint,
    git_head
  };
  bool comparison_visible_ = false;
  bool comparison_git_pending_ = false;
  bool comparison_git_request_queued_ = false;
  bool comparison_stale_ = false;
  bool comparison_current_read_failed_ = false;
  std::uint64_t note_content_generation_ = 0;
  std::uint64_t comparison_request_generation_ = 0;
  std::string observed_markdown_text_;
  ComparisonBaseline comparison_baseline_ = ComparisonBaseline::edit_checkpoint;
  std::filesystem::path comparison_path_;
  std::string comparison_text_;
  std::filesystem::path edit_checkpoint_path_;
  std::optional<std::string> edit_checkpoint_text_;
  std::unordered_map<std::string, std::string> detached_edit_checkpoints_;
  std::optional<std::string> comparison_edit_checkpoint_text_;
  notepp::git_sync::HeadContentResult comparison_git_result_;
  std::future<notepp::git_sync::HeadContentResult> comparison_git_future_;

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
