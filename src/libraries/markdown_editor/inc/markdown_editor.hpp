#pragma once

#include <imgui.h>

#include <string>
#include <utility>

namespace MarkdownEditor
{
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

/**
 * @brief Insert a checklist item ("- [ ] ") at the cursor position.
 *
 * Updates the format state's cursor and selection so the new item is selected.
 */
void insert_checklist_item_at_cursor(std::string &text, MdFormatState &fmt);

/**
 * @brief Insert a markdown table of the given size at the cursor.
 *
 * The header row's first cell becomes the new selection so the user can type
 * a column name immediately.
 */
void insert_markdown_table_at_cursor(std::string &text, MdFormatState &fmt, int rows = 1, int cols = 2);

/**
 * @brief Prefix every line in the selection with "> ".
 *
 * @return true if the selection was non-empty and got quoted.
 */
void apply_note_quote(std::string &s, int &sel_a, int &sel_b);

/**
 * @brief Wrap the selection with arbitrary left/right markers (e.g. ** for bold).
 *
 * The selection is updated to point at the wrapped content.
 */
void apply_wrap_string(std::string &s, int &sel_a, int &sel_b,
                       const std::string &left, const std::string &right);

/**
 * @brief Wrap the selection with [color=#RRGGBB]...[/color] tags.
 */
void apply_color_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &hex_color);

/**
 * @brief Convert an ImVec4 color (0..1 per channel) to "#RRGGBB".
 */
std::string rgba_to_hex(ImVec4 c);

/**
 * @brief Return [start, end) byte indices of the line containing cursor_pos.
 */
std::pair<int, int> line_bounds_from_cursor(const std::string &text, int cursor_pos);

/**
 * @brief Find the [start, end) bounds of the word under a double click.
 *
 * Honors the current selection range; falls back to a single position if no
 * word char is found near the cursor.
 */
std::pair<int, int> word_bounds_from_double_click(const std::string &text,
                                                  int cursor_pos,
                                                  int sel_start, int sel_end);

/**
 * @brief Decide whether an undo snapshot should be recorded for this edit.
 *
 * Returns true when the edit crosses a "word boundary" or is otherwise a
 * non-trivial change (insert/delete of more than one character, replacement).
 */
bool should_push_word_granular_undo(const std::string &before,
                                    const std::string &after,
                                    MdFormatState &st);

/**
 * @brief Normalize a string after reading it from an ImGui input buffer.
 *
 * Truncates at the first NUL byte if the buffer was over-allocated.
 */
void normalize_input_text_buffer(std::string &s);
} // namespace MarkdownEditor