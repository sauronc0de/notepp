#pragma once

#include "markdown_editor.hpp"
#include "markdown_tables.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
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

// Byte range in Markdown source. The end offset is always exclusive.
struct SourceRange
{
  std::size_t begin = 0;
  std::size_t end = 0;

  [[nodiscard]] bool empty() const noexcept { return begin >= end; }
};

[[nodiscard]] SourceRange clamp_source_range(std::size_t document_size,
                                             std::size_t offset,
                                             std::size_t length) noexcept;
[[nodiscard]] bool source_ranges_intersect(SourceRange lhs, SourceRange rhs) noexcept;
[[nodiscard]] bool is_plain_markdown_text(std::string_view text) noexcept;
// Returns the exclusive end of a backtick/tilde fenced block beginning at
// line_start. Unterminated fences extend to the end of the document.
[[nodiscard]] std::optional<std::size_t> markdown_fence_block_end(
    std::string_view markdown, std::size_t line_start) noexcept;

// Re-export editor types/functions from the markdown_editor library for
// backward compatibility with existing call sites.
using MdFormatState = MarkdownEditor::MdFormatState;
using MdEditorUserData = MarkdownEditor::MdEditorUserData;

using MarkdownEditor::apply_color_wrap_string;
using MarkdownEditor::apply_note_quote;
using MarkdownEditor::apply_wrap_string;
using MarkdownEditor::insert_checklist_item_at_cursor;
using MarkdownEditor::insert_markdown_table_at_cursor;
using MarkdownEditor::line_bounds_from_cursor;
using MarkdownEditor::normalize_input_text_buffer;
using MarkdownEditor::rgba_to_hex;
using MarkdownEditor::should_push_word_granular_undo;
using MarkdownEditor::word_bounds_from_double_click;

// Re-export markdown table parsing/building helpers from the markdown_tables
// library for backward compatibility with existing call sites.
using ParsedMarkdownTable = MarkdownTables::ParsedMarkdownTable;
using MarkdownTables::build_md_table_line;
using MarkdownTables::build_md_table_markdown;
using MarkdownTables::build_md_table_separator;
using MarkdownTables::is_md_table_separator;
using MarkdownTables::normalize_table_cell_value;
using MarkdownTables::split_md_table_cells;
using MarkdownTables::try_parse_markdown_table;

int md_editor_cb(ImGuiInputTextCallbackData *data);
bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out);
void set_preview_document_path(std::string_view path);
void request_preview_heading(std::string_view document_path,
                             std::string_view heading_path,
                             std::size_t heading_occurrence_index);
// Reveal and visibly mark a source range in the matching preview document.
// A zero length keeps the legacy scroll-only behavior.
void request_preview_source_offset(std::string_view document_path, std::size_t offset,
                                   std::size_t length = 0);
void notify_document_moved(const std::filesystem::path &from,
                           const std::filesystem::path &to);
void notify_document_saved(const std::filesystem::path &path);
void set_preview_state_path(const std::filesystem::path &path);
bool flush_preview_state();
std::string last_persistence_error();
PreviewRenderResult render_preview_with_task_checkboxes_ex(std::string &markdown);
bool render_preview_with_task_checkboxes(std::string &markdown);
PreviewHeaderStateSummary summarize_preview_header_states(std::string_view document_path, std::string_view markdown);
bool set_all_preview_headers_open(std::string_view document_path, std::string_view markdown, bool open);
std::string capture_preview_state_snapshot();
void apply_preview_state_snapshot(std::string_view snapshot);
} // namespace MarkdownSupport
