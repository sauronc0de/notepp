#pragma once
#include <string>
#include <string_view>
#include <filesystem>

#include <imgui.h>

struct ImFont;

struct MarkdownHoverPreviewData
{
  ImVec2 mouse_pos = ImVec2(0.0f, 0.0f);
  std::string title;
  std::string path;
  std::string body;
  bool link_hovered = false;
  // Byte range of `body` in the source file; std::string_view::npos when
  // not known. Used by callers to splice edits back into the file.
  size_t section_start = std::string_view::npos;
  size_t section_end = std::string_view::npos;
};

struct MarkdownView
{
  struct TextureHandle
  {
    ImTextureID id = (ImTextureID)0;
    float width = 0.0f;
    float height = 0.0f;
    bool valid = false;
  };

  struct ImageContextResult
  {
    bool consumed_right_click = false;
    bool markdown_changed = false;
  };

  static void set_fonts(ImFont *regular, ImFont *italic, ImFont *bold);
  static void set_render_width(float width);
  static void set_document_path(std::filesystem::path path);
  static void set_hover_preview_enabled(bool enabled);
  static bool take_hover_preview(MarkdownHoverPreviewData &out);
  // Update the body of the currently active hover preview in place. Also
  // adjusts section_end so the next caller of take_hover_preview sees a
  // consistent (start, end) range covering the new body. No-op when no
  // preview is active.
  static void update_hover_preview_body(std::string new_body);
  static void clear_hover_preview();
  static void render(std::string_view markdown);
  static void render_inline(std::string_view markdown_inline);
  static void set_assets_path(std::filesystem::path path);
  static TextureHandle get_or_load_texture(const std::filesystem::path &path);
  static void begin_sidebar_thumbnail_frame();
  static TextureHandle get_or_load_sidebar_thumbnail(const std::filesystem::path &path);
  static bool sidebar_thumbnail_work_deferred();
  static void clear_sidebar_thumbnail_cache();
  static void shutdown_sidebar_thumbnail_cache();
  static ImageContextResult render_image_context_menu(std::string &markdown);
};
