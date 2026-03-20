#pragma once
#include <string>
#include <string_view>

#include <imgui.h>

struct ImFont;

struct MarkdownHoverPreviewData
{
  ImVec2 mouse_pos = ImVec2(0.0f, 0.0f);
  std::string title;
  std::string path;
  std::string body;
  bool link_hovered = false;
};

struct MarkdownView
{
  static void set_fonts(ImFont *regular, ImFont *italic, ImFont *bold);
  static void set_render_width(float width);
  static void set_document_path(std::string_view path);
  static void set_data_root(std::string_view path);
  static void set_hover_preview_enabled(bool enabled);
  static bool take_hover_preview(MarkdownHoverPreviewData &out);
  static void clear_hover_preview();
  static void render(std::string_view markdown);
  static void render_inline(std::string_view markdown_inline);
};
