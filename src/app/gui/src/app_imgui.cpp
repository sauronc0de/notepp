// ── app_imgui.cpp ───────────────────────────────────────────────────────────
//
// ImGui and font setup for the App. Extracted from app.cpp so the
// application entry point file does not need to carry the full ImGui
// bring-up sequence.

#include "app.hpp"

#include <stdexcept>
#include <string>

#include <imgui.h>
#ifdef IMGUI_ENABLE_FREETYPE
#include <imgui_freetype.h>
#endif
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <SDL_opengl.h>

#include "markdown_view.hpp"
#include "note_ui.hpp"

namespace
{
constexpr const char *kGlslVersion = "#version 150";
constexpr float kUiFontSize = 14.0f;
} // namespace

void App::init_imgui()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;

  // Keep ImGui's embedded font for the fixed-width terminal grid. The main
  // interface uses the proportional families loaded below.
  ImFontConfig terminal_font_config;
  terminal_font_config.SizePixels = kUiFontSize;
  terminal_font_config.PixelSnapH = true;
  ImFont *font_terminal = io.Fonts->AddFontDefault(&terminal_font_config);

  const std::string noto_sans_mono_path = (config_.assetsPath / "fonts" / "NotoSansMono-Regular.ttf").string();

  auto merge_emoji_fallback = [&](const char *emoji_font_path) {
    ImFontConfig emoji_cfg;
    emoji_cfg.MergeMode = true;
    emoji_cfg.PixelSnapH = true;
    emoji_cfg.GlyphMinAdvanceX = kUiFontSize;
#ifdef IMGUI_ENABLE_FREETYPE
    emoji_cfg.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_LoadColor;
#endif
    static const ImWchar emoji_ranges[] = {
        0x200D, 0x200D,   // ZWJ
        0x2600, 0x27BF,   // misc symbols + dingbats
        0xFE0E, 0xFE0F,   // variation selectors
        0x1F300, 0x1FAFF, // emoji blocks
        0};
    io.Fonts->AddFontFromFileTTF(emoji_font_path, kUiFontSize, &emoji_cfg, emoji_ranges);
  };

  auto merge_box_drawing_fallback = [&](ImFont *destination_font) {
    if(destination_font == nullptr) return;

    ImFontConfig box_drawing_cfg;
    box_drawing_cfg.MergeMode = true;
    box_drawing_cfg.PixelSnapH = true;
    static const ImWchar box_drawing_ranges[] = {0x2500, 0x257F, 0};
    io.Fonts->AddFontFromFileTTF(noto_sans_mono_path.c_str(), kUiFontSize, &box_drawing_cfg,
                                 box_drawing_ranges);
  };

  merge_box_drawing_fallback(font_terminal);

  const std::string roboto_path = (config_.assetsPath / "fonts" / "Roboto-Medium.ttf").string();
  const std::string opensans_path = (config_.assetsPath / "fonts" / "opensans.ttf").string();
  const std::string italic_path = (config_.assetsPath / "fonts" / "Roboto-Italic.ttf").string();
  const std::string bold_path = (config_.assetsPath / "fonts" / "Roboto-Bold.ttf").string();
  const std::string twemoji_path = (config_.assetsPath / "fonts" / "twemoji.ttf").string();
  ImFont *font_regular = io.Fonts->AddFontFromFileTTF(roboto_path.c_str(), kUiFontSize);
  if(font_regular) default_font_path_ = roboto_path;
  if(!font_regular)
  {
    font_regular = io.Fonts->AddFontFromFileTTF(opensans_path.c_str(), kUiFontSize);
    if(font_regular) default_font_path_ = opensans_path;
  }
  if(font_regular)
  {
    merge_box_drawing_fallback(font_regular);
    merge_emoji_fallback(twemoji_path.c_str());
  }

  ImFont *font_italic = io.Fonts->AddFontFromFileTTF(italic_path.c_str(), kUiFontSize);
  if(font_italic)
  {
    merge_box_drawing_fallback(font_italic);
    merge_emoji_fallback(twemoji_path.c_str());
  }

  ImFont *font_bold = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), kUiFontSize);
  if(font_bold)
  {
    merge_box_drawing_fallback(font_bold);
    merge_emoji_fallback(twemoji_path.c_str());
  }

  // Fallback if you don’t have files yet:
  if(!font_regular) font_regular = io.Fonts->Fonts.front();
  if(!font_italic) font_italic = font_regular;
  if(!font_bold) font_bold = font_regular;

  // Ensure all ImGui widgets (including InputTextMultiline editor) use this font atlas entry.
  // Without this, ImGui may keep using AddFontDefault() and render unknown glyphs as '?'.
  io.FontDefault = font_regular;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  if(detached_note_windows_enabled_)
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  else
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

  ImGui::StyleColorsDark();
  if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  if(!ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_))
    throw std::runtime_error("ImGui_ImplSDL2_InitForOpenGL failed");

  if(!ImGui_ImplOpenGL3_Init(kGlslVersion))
    throw std::runtime_error("ImGui_ImplOpenGL3_Init failed");

  NoteUi::init_icon_shader();

  font_regular_ = font_regular;
  font_italic_ = font_italic;
  font_bold_ = font_bold;
  font_terminal_ = font_terminal != nullptr ? font_terminal : font_regular;
  MarkdownView::set_fonts(font_regular, font_italic, font_bold);
}
