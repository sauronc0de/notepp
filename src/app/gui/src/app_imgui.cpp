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

void apply_notepp_style()
{
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  // Keep the base style in one place so individual widgets only need local
  // overrides for behavior-specific states (for example, folder accents).
  style.WindowPadding = ImVec2(10.0f, 9.0f);
  style.FramePadding = ImVec2(8.0f, 5.0f);
  style.ItemSpacing = ImVec2(7.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
  style.CellPadding = ImVec2(7.0f, 4.0f);
  style.IndentSpacing = 17.0f;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 10.0f;

  style.WindowRounding = 4.0f;
  style.ChildRounding = 3.0f;
  style.FrameRounding = 3.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 3.0f;
  style.GrabRounding = 3.0f;
  style.TabRounding = 3.0f;

  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;

  // Solid surfaces and a restrained blue accent avoid expensive visual
  // effects while keeping focus and selection states clear.
  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(0.886f, 0.906f, 0.929f, 1.0f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.412f, 0.455f, 0.502f, 1.0f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.086f, 1.0f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.094f, 0.125f, 1.0f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.106f, 0.133f, 0.173f, 1.0f);
  colors[ImGuiCol_Border] = ImVec4(0.161f, 0.196f, 0.239f, 1.0f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  colors[ImGuiCol_FrameBg] = ImVec4(0.106f, 0.133f, 0.173f, 1.0f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.133f, 0.169f, 0.216f, 1.0f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.141f, 0.212f, 0.310f, 1.0f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.106f, 0.133f, 0.173f, 1.0f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.118f, 0.153f, 0.204f, 1.0f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.075f, 0.094f, 0.125f, 1.0f);

  colors[ImGuiCol_Button] = ImVec4(0.106f, 0.133f, 0.173f, 1.0f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.133f, 0.169f, 0.216f, 1.0f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.141f, 0.212f, 0.310f, 1.0f);
  colors[ImGuiCol_Header] = ImVec4(0.133f, 0.169f, 0.216f, 1.0f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.153f, 0.204f, 0.267f, 1.0f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.141f, 0.212f, 0.310f, 1.0f);

  const ImVec4 accent(0.298f, 0.553f, 1.0f, 1.0f);
  colors[ImGuiCol_CheckMark] = accent;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.384f, 0.612f, 1.0f, 1.0f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.298f, 0.553f, 1.0f, 0.35f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.298f, 0.553f, 1.0f, 0.65f);
  colors[ImGuiCol_ResizeGripActive] = accent;
  colors[ImGuiCol_Separator] = ImVec4(0.145f, 0.176f, 0.216f, 1.0f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.298f, 0.553f, 1.0f, 0.65f);
  colors[ImGuiCol_SeparatorActive] = accent;

  colors[ImGuiCol_Tab] = ImVec4(0.075f, 0.094f, 0.125f, 1.0f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.133f, 0.169f, 0.216f, 1.0f);
  colors[ImGuiCol_TabActive] = ImVec4(0.141f, 0.212f, 0.310f, 1.0f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.055f, 0.067f, 0.086f, 1.0f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.161f, 0.196f, 0.239f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.216f, 0.267f, 0.329f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = accent;
}
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
  const std::string noto_sans_symbols2_path =
      (config_.assetsPath / "fonts" / "NotoSansSymbols2-Regular.ttf").string();
  const std::string dejavu_sans_mono_path =
      (config_.assetsPath / "fonts" / "DejaVuSansMono.ttf").string();

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

  auto merge_noto_sans_mono_fallback = [&](ImFont *destination_font) {
    if(destination_font == nullptr) return;

    ImFontConfig noto_sans_mono_cfg;
    noto_sans_mono_cfg.MergeMode = true;
    noto_sans_mono_cfg.PixelSnapH = true;
    // Keep the fallback restricted to the symbols needed by the editor and
    // terminal. Noto Sans Mono supplies the cell-width box and punctuation
    // glyphs without changing the primary font's normal Latin coverage.
    static const ImWchar noto_sans_mono_ranges[] = {
        0x00B7, 0x00B7, // middle dot
        0x2022, 0x2022, // bullet
        0x2026, 0x2026, // horizontal ellipsis
        0x2190, 0x21B3, // arrows
        0x22EF, 0x22EF, // midline horizontal ellipsis
        0x2500, 0x257F, // box drawing
        0x25AA, 0x25AB, // small squares
        0x25B6, 0x25B6, // right-pointing triangle
        0x25B8, 0x25B9, // small right-pointing triangles
        0x25BA, 0x25BA, // right-pointing pointer
        0x25C6, 0x25C7, // diamonds
        0};
    io.Fonts->AddFontFromFileTTF(noto_sans_mono_path.c_str(), kUiFontSize, &noto_sans_mono_cfg,
                                 noto_sans_mono_ranges);
  };

  auto merge_noto_sans_symbols2_fallback = [&](ImFont *destination_font) {
    if(destination_font == nullptr) return;

    ImFontConfig noto_sans_symbols2_cfg;
    noto_sans_symbols2_cfg.MergeMode = true;
    noto_sans_symbols2_cfg.PixelSnapH = true;
    // Symbols 2 supplies the check marks absent from Noto Sans Mono. Keep the
    // other requested ranges here as well so the fallback remains explicit if
    // the mono font is replaced with a narrower build in the future.
    static const ImWchar noto_sans_symbols2_ranges[] = {
        0x21AF, 0x21AF, // downwards zigzag arrow
        0x25AA, 0x25AB, // small squares
        0x25B6, 0x25B6, // right-pointing triangle
        0x25B8, 0x25B9, // small right-pointing triangles
        0x25BA, 0x25BA, // right-pointing pointer
        0x25C6, 0x25C7, // diamonds
        0x2713, 0x2714, // check marks
        0x2717, 0x2718, // ballot marks
        0};
    io.Fonts->AddFontFromFileTTF(noto_sans_symbols2_path.c_str(), kUiFontSize, &noto_sans_symbols2_cfg,
                                 noto_sans_symbols2_ranges);
  };

  auto merge_dejavu_sans_mono_fallback = [&](ImFont *destination_font) {
    if(destination_font == nullptr) return;

    ImFontConfig dejavu_sans_mono_cfg;
    dejavu_sans_mono_cfg.MergeMode = true;
    dejavu_sans_mono_cfg.PixelSnapH = true;
    // DejaVu Sans Mono supplies the two symbols still missing from the Noto
    // fallbacks without changing the primary font's normal coverage.
    static const ImWchar dejavu_sans_mono_ranges[] = {
        0x21B3, 0x21B3, // downwards arrow with tip rightwards
        0x22EF, 0x22EF, // midline horizontal ellipsis
        0};
    io.Fonts->AddFontFromFileTTF(dejavu_sans_mono_path.c_str(), kUiFontSize, &dejavu_sans_mono_cfg,
                                 dejavu_sans_mono_ranges);
  };

  merge_noto_sans_mono_fallback(font_terminal);
  merge_noto_sans_symbols2_fallback(font_terminal);
  merge_dejavu_sans_mono_fallback(font_terminal);

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
    merge_noto_sans_mono_fallback(font_regular);
    merge_noto_sans_symbols2_fallback(font_regular);
    merge_dejavu_sans_mono_fallback(font_regular);
    merge_emoji_fallback(twemoji_path.c_str());
  }

  ImFont *font_italic = io.Fonts->AddFontFromFileTTF(italic_path.c_str(), kUiFontSize);
  if(font_italic)
  {
    merge_noto_sans_mono_fallback(font_italic);
    merge_noto_sans_symbols2_fallback(font_italic);
    merge_dejavu_sans_mono_fallback(font_italic);
    merge_emoji_fallback(twemoji_path.c_str());
  }

  ImFont *font_bold = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), kUiFontSize);
  if(font_bold)
  {
    merge_noto_sans_mono_fallback(font_bold);
    merge_noto_sans_symbols2_fallback(font_bold);
    merge_dejavu_sans_mono_fallback(font_bold);
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

  apply_notepp_style();
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
