#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
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

using TerminalCommandHandler = std::function<void(std::string_view)>;

void set_widget_document_path(std::filesystem::path path);
void notify_document_moved(const std::filesystem::path &from,
                           const std::filesystem::path &to);
void set_terminal_command_handler(TerminalCommandHandler handler);
std::string last_persistence_error();

std::string capture_ui_state_snapshot();
void apply_ui_state_snapshot(std::string_view snapshot);

RenderResult try_render_ui_block(std::string &markdown, size_t fence_start, size_t fence_line_end, size_t block_end);

std::string resolve_ui_mermaid_template(std::string_view note_markdown, std::string_view template_body);
} // namespace MarkdownWidgets
