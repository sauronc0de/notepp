#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace MarkdownWidgets
{
struct RenderResult
{
  bool handled = false;
  bool markdown_changed = false;
  bool preview_state_changed = false;
  bool consumed_right_click = false;
};

void set_widget_document_path(std::filesystem::path path);

std::string capture_ui_state_snapshot();
void apply_ui_state_snapshot(std::string_view snapshot);

RenderResult try_render_ui_block(std::string &markdown, size_t fence_start, size_t fence_line_end, size_t block_end);

std::string resolve_ui_mermaid_template(std::string_view note_markdown, std::string_view template_body);

// Strict JSON (RFC 8259) helpers. The input and output use the same
// relaxed text syntax as UI block value literals (see Value / serialize_value),
// so the widget can round-trip through these without exposing internal types.
bool try_parse_strict_json(std::string_view text, std::string &value_text, std::string &error);
std::string value_to_compact_json(std::string_view value_text, std::string &error);
std::string value_to_pretty_json(std::string_view value_text, std::string &error);
} // namespace MarkdownWidgets
