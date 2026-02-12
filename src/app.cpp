#include "app.hpp"
#include "helpers.hpp"
#include "markdown_view.hpp"

#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include <SDL.h>
#include <SDL_opengl.h>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

namespace
{
constexpr const char *kGlslVersion = "#version 150";
static bool palette_hovered = false;
static bool palette_focused = false;
struct MdSection
{
  int level = 0;               // 1..6
  std::string title;           // heading text
  std::string body;            // markdown until next heading of same/higher level
  std::vector<MdSection> kids; // nested headings
};

struct MdFormatState
{
  int sel_start = 0;
  int sel_end = 0;

  enum class Action
  {
    None,
    Italic,
    Bold,
    Strike,
    Code,
    Color
  } pending = Action::None;
  ImVec4 color = ImVec4(1, 0.6f, 0.2f, 1); // default
};

struct MdEditorUserData
{
  std::string *text = nullptr;
  MdFormatState *fmt = nullptr;
};

static void apply_wrap(ImGuiInputTextCallbackData *data, const char *left, const char *right)
{
  int a = data->SelectionStart;
  int b = data->SelectionEnd;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  data->InsertChars(b, right);
  data->InsertChars(a, left);

  // Keep selection around the original text (now shifted by left length)
  const int l = (int)strlen(left);
  data->SelectionStart = a + l;
  data->SelectionEnd = b + l;
  data->CursorPos = data->SelectionEnd;
}

static void apply_note_quote(std::string &s, int &sel_a, int &sel_b)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Expand to full lines for nicer behavior
  while(a > 0 && s[(size_t)a - 1] != '\n') --a;
  while(b < (int)s.size() && s[(size_t)b] != '\n') ++b;

  // Count lines and insert "> " at each line start.
  // We insert from start to end while tracking the shifting offset.
  int offset = 0;
  for(int i = a; i <= b;)
  {
    const int insert_pos = i + offset;
    s.insert((size_t)insert_pos, "> ");
    offset += 2;

    // Move to next line start
    size_t nl = s.find('\n', (size_t)(insert_pos + 2));
    if(nl == std::string::npos) break;
    i = (int)nl + 1 - offset; // convert back to original coordinate space
    if(i > b) break;
  }

  // Update selection to include the inserted prefixes
  sel_a = a;
  sel_b = b + offset;
}

static void apply_wrap_string(std::string &s, int &sel_a, int &sel_b,
                              const std::string &left, const std::string &right)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Insert right first (at higher index)
  s.insert((size_t)b, right);
  s.insert((size_t)a, left);

  // Update selection to remain around the original content
  a += (int)left.size();
  b += (int)left.size();
  sel_a = a;
  sel_b = b;
}

static std::string rgba_to_hex(ImVec4 c)
{
  auto clamp01 = [](float v) { return v < 0 ? 0.f : (v > 1 ? 1.f : v); };
  int r = (int)(clamp01(c.x) * 255.0f + 0.5f);
  int g = (int)(clamp01(c.y) * 255.0f + 0.5f);
  int b = (int)(clamp01(c.z) * 255.0f + 0.5f);

  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return std::string(buf);
}

static int md_editor_cb(ImGuiInputTextCallbackData *data)
{
  auto *st = static_cast<MdFormatState *>(data->UserData);

  // Track selection continuously
  st->sel_start = data->SelectionStart;
  st->sel_end = data->SelectionEnd;

  // // Apply a pending action inside the callback (safe)
  // if(st->pending != MdFormatState::Action::None)
  // {
  //   switch(st->pending)
  //   {
  //   case MdFormatState::Action::Italic:
  //     apply_wrap(data, "*", "*");
  //     break;
  //   case MdFormatState::Action::Bold:
  //     apply_wrap(data, "**", "**");
  //     break;
  //   case MdFormatState::Action::Strike:
  //     apply_wrap(data, "~~", "~~");
  //     break;
  //   case MdFormatState::Action::Code:
  //     apply_wrap(data, "`", "`");
  //     break;
  //   case MdFormatState::Action::Color: {
  //     const std::string hex = rgba_to_hex(st->color);
  //     const std::string left = "[color=" + hex + "]";
  //     const char *right = "[/color]";
  //     apply_wrap(data, left.c_str(), right);
  //   }
  //   break;
  //   default:
  //     break;
  //   }

  //   st->pending = MdFormatState::Action::None;
  // }

  return 0;
}
static bool parse_heading_line(std::string_view line, int &level_out, std::string_view &title_out)
{
  line = ltrim(line);
  int level = 0;
  while(level < 6 && level < (int)line.size() && line[level] == '#') level++;
  if(level == 0) return false;

  // require a space after hashes (common markdown rule)
  if((size_t)level >= line.size() || line[(size_t)level] != ' ') return false;

  std::string_view title = trim(line.substr((size_t)level + 1));
  if(title.empty()) title = "(untitled)";

  level_out = level;
  title_out = title;
  return true;
}

static MdSection parse_sections(std::string_view md)
{
  MdSection root; // level 0
  std::vector<MdSection *> stack;
  stack.push_back(&root);

  size_t pos = 0;
  auto take_line = [&](size_t &p) -> std::string_view {
    if(p >= md.size()) return {};
    size_t e = md.find('\n', p);
    if(e == std::string_view::npos) e = md.size();
    auto line = md.substr(p, e - p);
    p = (e < md.size()) ? e + 1 : e;
    return line;
  };

  while(pos < md.size())
  {
    std::string_view line = take_line(pos);

    int level = 0;
    std::string_view title;
    if(parse_heading_line(line, level, title))
    {
      // pop until parent has lower level
      while(!stack.empty() && stack.back()->level >= level) stack.pop_back();
      if(stack.empty()) stack.push_back(&root);

      // create node under current parent
      stack.back()->kids.push_back(MdSection{level, std::string(title), {}, {}});
      MdSection *added = &stack.back()->kids.back();
      stack.push_back(added);
    }
    else
    {
      // normal content belongs to current section
      stack.back()->body.append(line.data(), line.size());
      stack.back()->body.push_back('\n');
    }
  }

  return root;
}

} // namespace

int App::run()
{
  try
  {
    init_sdl_gl();
    init_imgui();

    while(running_)
    {
      frame_begin();
      frame_ui();
      frame_end();
    }

    shutdown();
    return 0;
  }
  catch(const std::exception &e)
  {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
    shutdown();
    return 1;
  }
}

void App::init_sdl_gl()
{
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

  // OpenGL 3.2 Core
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  window_ = SDL_CreateWindow(
      "Minimal ImGui Markdown",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1100, 700,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if(!window_)
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

  gl_context_ = SDL_GL_CreateContext(window_);
  if(!gl_context_)
    throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());

  SDL_GL_MakeCurrent(window_, gl_context_);
  SDL_GL_SetSwapInterval(1); // vsync
}

void App::init_imgui()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();

  // Pick a font family that has Bold/Italic files available.
  io.Fonts->AddFontDefault();

  // Example with Roboto (put these files in assets/fonts or wherever you want):
  // - Roboto-Regular.ttf
  // - Roboto-Italic.ttf
  // - Roboto-Bold.ttf
  // - Roboto-BoldItalic.ttf

  ImFont *font_regular = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Regular.ttf", 16.0f);
  ImFont *font_italic = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Italic.ttf", 16.0f);
  ImFont *font_bold = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Bold.ttf", 16.0f);

  // Fallback if you don’t have files yet:
  if(!font_regular) font_regular = io.Fonts->Fonts.front();
  if(!font_italic) font_italic = font_regular;
  if(!font_bold) font_bold = font_regular;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  if(!ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_))
    throw std::runtime_error("ImGui_ImplSDL2_InitForOpenGL failed");

  if(!ImGui_ImplOpenGL3_Init(kGlslVersion))
    throw std::runtime_error("ImGui_ImplOpenGL3_Init failed");

  MarkdownView::set_fonts(font_regular, font_italic, font_bold);
}

void App::shutdown()
{
  // Safe to call multiple times.
  if(ImGui::GetCurrentContext())
  {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }

  if(gl_context_)
  {
    SDL_GL_DeleteContext(gl_context_);
    gl_context_ = nullptr;
  }

  if(window_)
  {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  SDL_Quit();
}

void App::frame_begin()
{
  SDL_Event event;
  while(SDL_PollEvent(&event))
  {
    ImGui_ImplSDL2_ProcessEvent(&event);

    if(event.type == SDL_QUIT) running_ = false;
    if(event.type == SDL_WINDOWEVENT &&
       event.window.event == SDL_WINDOWEVENT_CLOSE &&
       event.window.windowID == SDL_GetWindowID(window_))
    {
      running_ = false;
    }
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
}
void App::frame_ui()
{
  // --- Dock host ---
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoDocking;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##DockHost", nullptr, host_flags);
  ImGui::PopStyleVar(2);

  ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();

  // --- Single window: "Note" (preview + edit overlay) ---
  // --- Note window: preview by default, double-click to edit, lose focus to preview ---
  ImGui::Begin("Note");
  ImGui::TextUnformatted("Double-click to edit. Click elsewhere to preview.");
  ImGui::Separator();

  static bool editing = false;

  ImGui::BeginChild("##note_body", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

  if(!editing)
  {
    // Preview mode (interactive)
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    MarkdownView::render(markdown_text_);
    ImGui::PopTextWrapPos();

    // Enter edit mode only on double click (single click does nothing)
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      editing = true;
      // Next frame, focus the editor widget
      ImGui::SetKeyboardFocusHere();
    }
  }
  else
  {
    // Plain text editor mode
    static MdFormatState fmt;

    // Plain text editor mode
    static MdEditorUserData ud{&markdown_text_, &fmt};

    ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput |
        ImGuiInputTextFlags_CallbackResize |
        ImGuiInputTextFlags_CallbackAlways;
    static bool refocus_editor = false;
    if(refocus_editor)
    {
      ImGui::SetKeyboardFocusHere();
      refocus_editor = false;
    }

    ImGui::InputTextMultiline(
        "##md",
        markdown_text_.data(),
        markdown_text_.capacity() + 1,
        ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
        flags,
        [](ImGuiInputTextCallbackData *data) -> int {
          auto *ud = static_cast<MdEditorUserData *>(data->UserData);

          // Handle resize
          if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
          {
            ud->text->resize((size_t)data->BufTextLen);
            data->Buf = ud->text->data();
            return 0;
          }

          // CallbackAlways: selection tracking + apply pending formatting
          // IMPORTANT: md_editor_cb expects UserData = MdFormatState*
          data->UserData = ud->fmt;
          return md_editor_cb(data);
        },
        &ud);

    // After the widget: show popup if selection is non-empty and editor is focused/active
    const bool editor_focused = ImGui::IsItemFocused();
    const bool editor_active = ImGui::IsItemActive();
    const bool editor_engaged = (editor_focused || editor_active);

    const int a = fmt.sel_start, b = fmt.sel_end;
    const bool has_selection = (a != b);

    static bool popup_was_open = false;
    static ImVec2 popup_pos = ImVec2(0, 0);
    // ---- Clickable floating formatting palette (tooltip-like) ----
    static bool show_palette = false;
    static ImVec2 palette_pos(0, 0);

    // When selection appears, show palette and pin position once.
    if((editor_focused || editor_active) && has_selection && !show_palette)
    {
      ImVec2 editor_min = ImGui::GetItemRectMin();
      palette_pos = ImVec2(editor_min.x + 10.0f, editor_min.y + 10.0f);
      show_palette = true;
    }

    // Hide when selection is cleared (but NOT when editor loses focus due to clicking the palette)
    if(!has_selection)
      show_palette = false;

    if(show_palette)
    {
      ImGui::SetNextWindowPos(palette_pos, ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.95f);

      ImGuiWindowFlags pal_flags =
          ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking; // NOTE: no Tooltip flag (Tooltip => NoInputs)

      ImGui::Begin("##md_format_palette", nullptr, pal_flags);
      palette_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      palette_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

      bool applied = false;

      if(ImGui::Button("Italic"))
      {
        apply_wrap_string(markdown_text_, fmt.sel_start, fmt.sel_end, "*", "*");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Bold"))
      {
        apply_wrap_string(markdown_text_, fmt.sel_start, fmt.sel_end, "**", "**");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Strike"))
      {
        apply_wrap_string(markdown_text_, fmt.sel_start, fmt.sel_end, "~~", "~~");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Note"))
      {
        apply_note_quote(markdown_text_, fmt.sel_start, fmt.sel_end);
        applied = true;
      }

      ImGui::Separator();

      ImGui::ColorEdit3("Color", (float *)&fmt.color, ImGuiColorEditFlags_NoInputs);
      ImGui::SameLine();
      if(ImGui::Button("Apply"))
      {
        const std::string hex = rgba_to_hex(fmt.color);
        apply_wrap_string(markdown_text_, fmt.sel_start, fmt.sel_end,
                          "[color=" + hex + "]", "[/color]");
        applied = true;
      }

      // Optional: close palette if user clicks outside both editor and palette
      const bool pal_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      const bool pal_active = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
      if(!pal_hovered && !pal_active && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !editor_focused && !editor_active)
        show_palette = false;

      ImGui::End();

      if(applied)
      {
        show_palette = false;
        refocus_editor = true; // your existing flag, so editor continues working
      }
    }
  }

  ImGui::EndChild();
  ImGui::End();
}

void App::frame_end()
{
  ImGui::Render();

  int display_w = 0, display_h = 0;
  SDL_GL_GetDrawableSize(window_, &display_w, &display_h);

  glViewport(0, 0, display_w, display_h);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(window_);
}
