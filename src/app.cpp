#include "app.hpp"
#include "helpers.hpp"
#include "markdown_view.hpp"

#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <filesystem>

#include <SDL.h>
#include <SDL_opengl.h>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

namespace
{
constexpr const char *kGlslVersion = "#version 150";
constexpr const char *kDefaultStateFile = DATA_PATH "/note.md";
constexpr const char *kStateMetaFile = DATA_PATH "/current_note_path.txt";
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
  int cursor_pos = 0;

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
  st->cursor_pos = data->CursorPos;

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

static void normalize_input_text_buffer(std::string &s)
{
  // ImGui edits the underlying char buffer directly; keep std::string::size() in sync.
  if(s.empty()) return;
  const size_t max_len = s.capacity() + 1;
  const size_t n = strnlen(s.data(), max_len);
  if(n <= s.size() || n <= s.capacity()) s.resize(n);
}

static std::string sanitize_note_filename(std::string title)
{
  for(char &c : title)
  {
    const bool bad =
        c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|';
    if(bad) c = '_';
  }

  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while(!title.empty() && is_space(title.front())) title.erase(title.begin());
  while(!title.empty() && is_space(title.back())) title.pop_back();
  if(title.empty()) title = "note";
  return title;
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

static bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

  if(i >= line.size() || (line[i] != '-' && line[i] != '*')) return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;

  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;

  check_col_out = i + 1;
  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;
  label_out = line.substr(i);
  return true;
}

static bool render_preview_with_task_checkboxes(std::string &markdown)
{
  bool changed = false;
  std::string normal_chunk;
  normal_chunk.reserve(markdown.size());

  auto flush_chunk = [&]() {
    if(normal_chunk.empty()) return;
    MarkdownView::render(normal_chunk);
    normal_chunk.clear();
  };

  size_t pos = 0;
  while(pos < markdown.size())
  {
    const size_t line_start = pos;
    size_t line_end = markdown.find('\n', pos);
    const bool has_newline = (line_end != std::string::npos);
    if(!has_newline) line_end = markdown.size();

    std::string_view line(markdown.data() + line_start, line_end - line_start);
    size_t check_col = 0;
    std::string_view label;
    if(parse_task_line(line, check_col, label))
    {
      flush_chunk();

      bool checked = (line[check_col] == 'x' || line[check_col] == 'X');
      ImGui::PushID((int)line_start);
      if(ImGui::Checkbox("##task", &checked))
      {
        markdown[line_start + check_col] = checked ? 'x' : ' ';
        changed = true;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(label.data(), label.data() + label.size());
      ImGui::PopID();
    }
    else
    {
      normal_chunk.append(line.data(), line.size());
      if(has_newline) normal_chunk.push_back('\n');
    }

    pos = has_newline ? line_end + 1 : line_end;
  }

  flush_chunk();
  return changed;
}

} // namespace

int App::run()
{
  try
  {
    init_sdl_gl();
    init_imgui();
    load_state();

    while(running_)
    {
      frame_begin();
      frame_ui();
      frame_end();
    }

    save_state();
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

  ImFont *font_regular = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Medium.ttf", 16.0f);
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

void App::load_state()
{
  {
    std::ifstream meta(kStateMetaFile);
    std::string saved_path;
    if(meta && std::getline(meta, saved_path) && !saved_path.empty())
      state_file_path_ = saved_path;
    else
      state_file_path_ = kDefaultStateFile;
  }

  std::ifstream in(state_file_path_, std::ios::binary);
  if(!in) return;

  markdown_text_.assign((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

  const std::filesystem::path p(state_file_path_);
  const std::string stem = p.stem().string();
  if(!stem.empty()) note_title_ = stem;
}

void App::save_state() const
{
  std::ofstream out(state_file_path_, std::ios::binary | std::ios::trunc);
  if(!out) return;
  out << markdown_text_;

  std::ofstream meta(kStateMetaFile, std::ios::trunc);
  if(meta) meta << state_file_path_;
}

void App::rename_note_storage_for_title(const std::string &new_title)
{
  const std::string safe_title = sanitize_note_filename(new_title);
  std::filesystem::path new_path = std::filesystem::path(DATA_PATH) / (safe_title + ".md");

  if(new_path.string() == state_file_path_)
  {
    note_title_ = safe_title;
    return;
  }

  std::error_code ec;
  std::filesystem::path current_path(state_file_path_);
  if(std::filesystem::exists(current_path, ec))
  {
    if(std::filesystem::exists(new_path, ec))
      std::filesystem::remove(new_path, ec);
    std::filesystem::rename(current_path, new_path, ec);
  }

  state_file_path_ = new_path.string();
  note_title_ = safe_title;
  save_state();
}

void App::frame_begin()
{
  SDL_Event event;
  while(SDL_PollEvent(&event))
  {
    if(event.type == SDL_QUIT) running_ = false;
    if(event.type == SDL_WINDOWEVENT &&
       event.window.event == SDL_WINDOWEVENT_CLOSE &&
       event.window.windowID == SDL_GetWindowID(window_))
    {
      running_ = false;
    }

    // While editing, keep Esc out of InputText so it doesn't cancel/revert the latest edit.
    if(editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_ESCAPE)
    {
      request_exit_edit_mode_ = true;
      continue;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
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
  static float note_window_height = 360.0f;
  auto compute_edit_window_height = [&]() -> float {
    const ImGuiStyle &st = ImGui::GetStyle();
    const float title_bar_h = ImGui::GetFontSize() + st.FramePadding.y * 2.0f;
    const int line_count = 1 + (int)std::count(markdown_text_.begin(), markdown_text_.end(), '\n');
    const float text_h = line_count * ImGui::GetTextLineHeightWithSpacing();
    const float input_h = text_h + st.FramePadding.y * 2.0f + 10.0f;
    return std::max(140.0f, title_bar_h + st.WindowPadding.y * 2.0f + input_h);
  };

  if(editing_mode_) note_window_height = compute_edit_window_height();

  ImGui::SetNextWindowSizeConstraints(
      ImVec2(320.0f, note_window_height),
      ImVec2(FLT_MAX, note_window_height));

  std::string note_window_label = note_title_ + "###NoteWindow";
  ImGui::Begin(
      note_window_label.c_str(),
      nullptr,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  // Right click on title bar to rename the note window title.
  static bool open_rename_popup = false;
  static char rename_buf[256] = {};
  const ImVec2 win_pos = ImGui::GetWindowPos();
  const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
  const ImVec2 mouse_pos = ImGui::GetMousePos();
  const bool mouse_on_title =
      mouse_pos.x >= win_pos.x &&
      mouse_pos.x <= (win_pos.x + ImGui::GetWindowWidth()) &&
      mouse_pos.y >= win_pos.y &&
      mouse_pos.y <= (win_pos.y + title_bar_h);
  if(mouse_on_title && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
  {
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", note_title_.c_str());
    open_rename_popup = true;
  }
  if(open_rename_popup)
  {
    ImGui::OpenPopup("Rename Note");
    open_rename_popup = false;
  }
  if(ImGui::BeginPopup("Rename Note"))
  {
    ImGui::TextUnformatted("Nom de la finestra:");
    ImGui::SetNextItemWidth(260.0f);
    if(ImGui::InputText("##rename_note_title", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      normalize_input_text_buffer(markdown_text_);
      rename_note_storage_for_title(rename_buf);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Apply"))
    {
      normalize_input_text_buffer(markdown_text_);
      rename_note_storage_for_title(rename_buf);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  static bool show_palette = false;
  static bool refocus_editor = false;
  static MdFormatState fmt;
  static MdEditorUserData ud{&markdown_text_, &fmt};
  ud.text = &markdown_text_;

  if(!editing_mode_)
  {
    // Preview mode (interactive)
    const float start_y = ImGui::GetCursorPosY();
    const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f);
    MarkdownView::set_render_width(preview_w);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    const bool task_changed = render_preview_with_task_checkboxes(markdown_text_);
    ImGui::PopTextWrapPos();
    if(task_changed) save_state();
    (void)start_y;

    // Enter edit mode only on double click (single click does nothing)
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      editing_mode_ = true;
      show_palette = false;
      // Next frame, focus the editor widget
      refocus_editor = true;
    }
  }
  else
  {
    // Plain text editor mode
    ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput |
        ImGuiInputTextFlags_CallbackResize |
        ImGuiInputTextFlags_CallbackAlways;
    if(refocus_editor)
    {
      ImGui::SetKeyboardFocusHere();
      refocus_editor = false;
    }

    const bool text_changed = ImGui::InputTextMultiline(
        "##md",
        markdown_text_.data(),
        markdown_text_.capacity() + 1,
        ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
        flags,
        [](ImGuiInputTextCallbackData *data) -> int {
          auto *ud = static_cast<MdEditorUserData *>(data->UserData);
          if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
          {
            ud->text->resize((size_t)data->BufTextLen);
            data->Buf = ud->text->data();
            return 0;
          }
          data->UserData = ud->fmt; // md_editor_cb expects MdFormatState*
          return md_editor_cb(data);
        },
        &ud);
    normalize_input_text_buffer(markdown_text_);
    if(text_changed) save_state();

    // After the widget: show popup if selection is non-empty and editor is focused/active
    const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    const int a = fmt.sel_start, b = fmt.sel_end;
    const bool has_selection = (a != b);
    const int sel_min = (a < b) ? a : b;
    const int sel_max = (a < b) ? b : a;
    static int anchor_sel_start = 0;
    static int anchor_sel_end = 0;

    // ---- Clickable floating formatting palette (tooltip-like) ----
    static ImVec2 palette_pos(0, 0);
    static bool palette_just_opened = false;

    if(has_selection)
    {
      anchor_sel_start = sel_min;
      anchor_sel_end = sel_max;
    }

    const bool has_anchor_selection = (anchor_sel_start != anchor_sel_end);

    // Open palette only with right-click while editing and text is selected.
    if(editor_hovered &&
       (has_selection || has_anchor_selection) &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      palette_pos = ImGui::GetMousePos();
      show_palette = true;
      palette_just_opened = true;
    }
    bool palette_hovered = false;
    if(show_palette && has_anchor_selection)
    {
      ImGui::SetNextWindowPos(palette_pos, ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.95f);
      if(palette_just_opened) ImGui::SetNextWindowFocus();

      ImGuiWindowFlags pal_flags =
          ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNavFocus |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking; // NOTE: no Tooltip flag (Tooltip => NoInputs)

      ImGui::Begin("##md_format_palette", nullptr, pal_flags);
      const bool pal_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      palette_hovered = pal_hovered;

      bool applied = false;

      if(ImGui::Button("Italic"))
      {
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Bold"))
      {
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Strike"))
      {
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Note"))
      {
        apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
        applied = true;
      }

      ImGui::Separator();

      ImGui::ColorEdit3("Color", (float *)&fmt.color, ImGuiColorEditFlags_NoInputs);
      ImGui::SameLine();
      if(ImGui::Button("Apply"))
      {
        const std::string hex = rgba_to_hex(fmt.color);
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end,
                          "[color=" + hex + "]", "[/color]");
        applied = true;
      }

      ImGui::End();

      if(applied)
      {
        normalize_input_text_buffer(markdown_text_);
        save_state();
        fmt.sel_start = anchor_sel_start;
        fmt.sel_end = anchor_sel_end;
      }
    }

    // Hide when clicking outside tooltip window (or if selection is gone).
    const bool any_popup_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool clicked_outside_palette =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
        !palette_hovered &&
        !any_popup_open &&
        !palette_just_opened;
    if(show_palette && (!has_anchor_selection || clicked_outside_palette))
    {
      show_palette = false;
      anchor_sel_start = 0;
      anchor_sel_end = 0;
    }
    palette_just_opened = false;
  }

  // Exit edit mode only with Esc.
  if(editing_mode_ && request_exit_edit_mode_)
  {
    normalize_input_text_buffer(markdown_text_);
    save_state();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    show_palette = false;
    refocus_editor = false;
  }

  if(editing_mode_)
    note_window_height = compute_edit_window_height();
  else
    note_window_height = std::max(140.0f, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);

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
