#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

namespace MarkdownSupport
{
struct PreviewRenderResult
{
  bool markdown_changed = false;
  bool preview_state_changed = false;
  bool consumed_right_click = false;
  bool consumed_double_click = false;
};

struct PreviewHeaderStateSummary
{
  bool has_headers = false;
  bool any_expanded = false;
  bool any_collapsed = false;
};

struct MdFormatState
{
  int sel_start = 0;
  int sel_end = 0;
  int cursor_pos = 0;

  enum class Action
  {
    None,
    Italic,
    Bold,
    Strike,
    Code,
    Color
  } pending = Action::None;

  ImVec4 color = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
  int selection_anchor = 0;
  int last_cursor_pos = 0;
  bool pending_select_range = false;
  int pending_sel_start = 0;
  int pending_sel_end = 0;
  bool typing_word_group = false;
  bool deleting_word_group = false;
  int last_edit_cursor = -1;
};

struct MdEditorUserData
{
  std::string *text = nullptr;
  MdFormatState *fmt = nullptr;
};

void insert_checklist_item_at_cursor(std::string &text, MdFormatState &fmt);
void insert_markdown_table_at_cursor(std::string &text, MdFormatState &fmt, int rows = 1, int cols = 2);
void apply_note_quote(std::string &s, int &sel_a, int &sel_b);
void apply_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &left, const std::string &right);
void apply_color_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &hex_color);
std::string rgba_to_hex(ImVec4 c);
std::pair<int, int> line_bounds_from_cursor(const std::string &text, int cursor_pos);
std::pair<int, int> word_bounds_from_double_click(const std::string &text, int cursor_pos, int sel_start, int sel_end);
bool should_push_word_granular_undo(const std::string &before, const std::string &after, MdFormatState &st);
int md_editor_cb(ImGuiInputTextCallbackData *data);
void normalize_input_text_buffer(std::string &s);
bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out);
void set_preview_document_path(std::string_view path);
PreviewRenderResult render_preview_with_task_checkboxes_ex(std::string &markdown);
bool render_preview_with_task_checkboxes(std::string &markdown);
PreviewHeaderStateSummary summarize_preview_header_states(std::string_view document_path, std::string_view markdown);
bool set_all_preview_headers_open(std::string_view document_path, std::string_view markdown, bool open);
std::string capture_preview_state_snapshot();
void apply_preview_state_snapshot(std::string_view snapshot);
} // namespace MarkdownSupport
