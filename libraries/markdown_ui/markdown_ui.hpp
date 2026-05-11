#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace MarkdownUi
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
} // namespace MarkdownUi
