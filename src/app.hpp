#pragma once
#include <string>
#include <vector>

struct SDL_Window;

class App
{
public:
  int run();

private:
  void init_sdl_gl();
  void init_imgui();
  void load_state();
  void save_state() const;
  void save_index() const;
  void load_note_content_for_active();
  void set_active_note(int folder_idx, int note_idx);
  void ensure_default_index();
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
  void shutdown();

  void frame_begin();
  void frame_ui();
  void frame_end();

  SDL_Window *window_ = nullptr;
  void *gl_context_ = nullptr;

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
  std::string note_title_ = "Note";
  std::string state_file_path_ = DATA_PATH "/note.md";
  struct NoteMeta
  {
    std::string title;
    std::string path;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float width = 520.0f;
    float height = 260.0f;
    bool has_layout = false;
    bool hidden = false;
  };
  struct FolderMeta
  {
    std::string name;
    bool use_custom_color = false;
    float color_r = 0.0f;
    float color_g = 0.0f;
    float color_b = 0.0f;
    std::vector<NoteMeta> notes;
  };
  std::vector<FolderMeta> folders_;
  std::vector<std::string> pending_fs_delete_paths_;
  int active_folder_idx_ = 0;
  int active_note_idx_ = 0;
  bool folder_overview_mode_ = false;
  mutable bool layout_dirty_ = false;
  std::vector<std::string> undo_stack_;
  std::vector<std::string> redo_stack_;

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
