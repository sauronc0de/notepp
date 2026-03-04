#pragma once
#include <string_view>

struct ImFont;

struct MarkdownView
{
  static void set_fonts(ImFont *regular, ImFont *italic, ImFont *bold);
  static void set_render_width(float width);
  static void render(std::string_view markdown);
};
