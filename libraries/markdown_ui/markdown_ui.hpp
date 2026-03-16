#pragma once

#include <cstddef>
#include <string>

namespace MarkdownUi
{
struct RenderResult
{
  bool handled = false;
  bool markdown_changed = false;
};

RenderResult try_render_ui_block(std::string &markdown, size_t fence_start, size_t fence_line_end, size_t block_end);
} // namespace MarkdownUi
