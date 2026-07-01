#pragma once

#include "markdown_editor.hpp"
#include "markdown_tables.hpp"

#include <filesystem>
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

// Re-export editor types/functions from the markdown_editor library for
// backward compatibility with existing call sites.
using MdFormatState    = MarkdownEditor::MdFormatState;
using MdEditorUserData  = MarkdownEditor::MdEditorUserData;

using MarkdownEditor::insert_checklist_item_at_cursor;
using MarkdownEditor::insert_markdown_table_at_cursor;
using MarkdownEditor::apply_note_quote;
using MarkdownEditor::apply_wrap_string;
using MarkdownEditor::apply_color_wrap_string;
using MarkdownEditor::rgba_to_hex;
using MarkdownEditor::line_bounds_from_cursor;
using MarkdownEditor::word_bounds_from_double_click;
using MarkdownEditor::should_push_word_granular_undo;
using MarkdownEditor::normalize_input_text_buffer;

// Re-export markdown table parsing/building helpers from the markdown_tables
// library for backward compatibility with existing call sites.
using ParsedMarkdownTable = MarkdownTables::ParsedMarkdownTable;
using MarkdownTables::split_md_table_cells;
using MarkdownTables::is_md_table_separator;
using MarkdownTables::try_parse_markdown_table;
using MarkdownTables::normalize_table_cell_value;
using MarkdownTables::build_md_table_line;
using MarkdownTables::build_md_table_separator;
using MarkdownTables::build_md_table_markdown;

int md_editor_cb(ImGuiInputTextCallbackData *data);
bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out);
void set_preview_document_path(std::string_view path);
void set_preview_state_path(const std::filesystem::path &path);
PreviewRenderResult render_preview_with_task_checkboxes_ex(std::string &markdown);
bool render_preview_with_task_checkboxes(std::string &markdown);
PreviewHeaderStateSummary summarize_preview_header_states(std::string_view document_path, std::string_view markdown);
bool set_all_preview_headers_open(std::string_view document_path, std::string_view markdown, bool open);
std::string capture_preview_state_snapshot();
void apply_preview_state_snapshot(std::string_view snapshot);
} // namespace MarkdownSupport
