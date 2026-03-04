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
  std::string make_note_path(const std::string &folder_name, const std::string &note_title) const;
  void sync_active_note_meta();
  void rename_note_storage_for_title(const std::string &new_title);
  void rename_note_by_index(int folder_idx, int note_idx, const std::string &new_title);
  void push_undo_snapshot();
  void apply_undo_snapshot();
  void shutdown();

  void frame_begin();
  void frame_ui();
  void frame_end();

  SDL_Window *window_ = nullptr;
  void *gl_context_ = nullptr;

  bool running_ = true;
  bool editing_mode_ = false;
  bool request_exit_edit_mode_ = false;
  bool request_undo_edit_ = false;
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
  };
  struct FolderMeta
  {
    std::string name;
    std::vector<NoteMeta> notes;
  };
  std::vector<FolderMeta> folders_;
  int active_folder_idx_ = 0;
  int active_note_idx_ = 0;
  bool folder_overview_mode_ = false;
  mutable bool layout_dirty_ = false;
  std::vector<std::string> undo_stack_;

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
