#include "markdown_view.hpp"
#include "helpers.hpp"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include "imgui_md.h"

namespace
{
struct Segment
{
  std::string text;
  bool colored = false;
  ImVec4 color{};
};

struct Chunk
{
  bool is_note = false;
  std::string text; // markdown to render
};

struct MdFonts
{
  ImFont *regular{};
  ImFont *italic{};
  ImFont *bold{};
};

static MdFonts g_fonts{};

struct TextureRecord
{
  GLuint texture_id = 0;
  ImVec2 size = ImVec2(0, 0);
  bool loaded = false;
};

static std::unordered_map<std::string, TextureRecord> g_image_cache{};
static float g_render_width = 0.0f;

static std::filesystem::path resolve_image_path(std::string_view href)
{
  const std::filesystem::path p(href);
  if(p.is_absolute() && std::filesystem::exists(p)) return p;

  const std::filesystem::path asset_root(ASSETS_PATH);
  const std::filesystem::path repo_root = asset_root.parent_path();

  std::vector<std::filesystem::path> candidates;
  candidates.push_back(p);
  candidates.push_back(repo_root / p);
  candidates.push_back(asset_root / p);

  if(starts_with(href, "assets/"))
  {
    std::filesystem::path rel = p.lexically_relative("assets");
    candidates.push_back(asset_root / rel);
  }

  for(const auto &c : candidates)
  {
    std::error_code ec;
    if(std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec))
      return c;
  }
  return {};
}

static TextureRecord load_texture_from_file(const std::filesystem::path &file)
{
  TextureRecord rec{};
  if(file.empty()) return rec;

  static bool img_ready = []() {
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    return true;
  }();
  (void)img_ready;

  SDL_Surface *loaded = IMG_Load(file.string().c_str());
  if(!loaded) return rec;

  SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if(!rgba) return rec;

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if(tex == 0)
  {
    SDL_FreeSurface(rgba);
    return rec;
  }

  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
  glBindTexture(GL_TEXTURE_2D, 0);

  rec.texture_id = tex;
  rec.size = ImVec2((float)rgba->w, (float)rgba->h);
  rec.loaded = true;
  SDL_FreeSurface(rgba);
  return rec;
}

// Remove a single leading '>' (and one optional following space) from a line
static std::string_view strip_quote_prefix(std::string_view line)
{
  line = ltrim(line);
  if(!line.empty() && line.front() == '>')
  {
    line.remove_prefix(1);
    if(!line.empty() && line.front() == ' ') line.remove_prefix(1);
  }
  return line;
}

// Collect blockquote chunks and normal chunks, detect note blocks by first meaningful line == "**Note**"
static std::vector<Chunk> split_note_blocks(std::string_view in)
{
  std::vector<Chunk> out;

  size_t i = 0;
  auto take_line = [&](size_t &pos) -> std::string_view {
    if(pos >= in.size()) return {};
    size_t e = in.find('\n', pos);
    if(e == std::string_view::npos) e = in.size();
    auto line = in.substr(pos, e - pos);
    pos = (e < in.size()) ? e + 1 : e;
    return line;
  };

  std::string normal_acc;

  auto flush_normal = [&]() {
    if(!normal_acc.empty())
    {
      out.push_back({false, std::move(normal_acc)});
      normal_acc.clear();
    }
  };

  while(i < in.size())
  {
    size_t line_start = i;
    std::string_view line = take_line(i);

    std::string_view t = ltrim(line);
    bool is_quote_line = starts_with(t, ">");

    if(!is_quote_line)
    {
      normal_acc.append(line.data(), line.size());
      normal_acc.push_back('\n');
      continue;
    }

    // Start of a quote block: keep consuming lines until an empty line.
    // Any line starting with '>' is treated as note content (prefix stripped).
    flush_normal();

    std::string quote_md;

    // Process first line + subsequent lines until blank line.
    size_t j = line_start;
    while(j < in.size())
    {
      std::string_view l = take_line(j);
      const std::string_view tl = trim(l);
      if(tl.empty())
      {
        // Blank line closes note mode.
        break;
      }

      std::string_view content = ltrim(l);
      if(starts_with(content, ">")) content = strip_quote_prefix(l);

      quote_md.append(content.data(), content.size());
      quote_md.push_back('\n');
    }

    i = j;
    out.push_back({true, std::move(quote_md)});
  }

  flush_normal();
  return out;
}

static bool parse_hex_color(std::string_view s, ImVec4 &out)
{
  // supports #RRGGBB or #RRGGBBAA
  if(s.size() != 7 && s.size() != 9) return false;
  if(s[0] != '#') return false;

  auto hex = [](char c) -> int {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };

  auto byte = [&](int i) -> int {
    int hi = hex(s[i]), lo = hex(s[i + 1]);
    if(hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
  };

  int r = byte(1), g = byte(3), b = byte(5);
  if(r < 0 || g < 0 || b < 0) return false;
  int a = 255;
  if(s.size() == 9)
  {
    a = byte(7);
    if(a < 0) return false;
  }

  out = ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
  return true;
}

static std::vector<Segment> split_color_spans(std::string_view in)
{
  std::vector<Segment> out;
  size_t i = 0;

  auto push_plain = [&](std::string_view sv) {
    if(!sv.empty()) out.push_back({std::string(sv), false, {}});
  };

  while(i < in.size())
  {
    size_t open = in.find("[color=", i);
    if(open == std::string_view::npos)
    {
      push_plain(in.substr(i));
      break;
    }

    // plain before tag
    push_plain(in.substr(i, open - i));

    // parse "[color=...]" header
    size_t close_bracket = in.find(']', open);
    if(close_bracket == std::string_view::npos)
    { // malformed
      push_plain(in.substr(open));
      break;
    }

    std::string_view spec = in.substr(open + 7, close_bracket - (open + 7)); // after "[color="
    ImVec4 c{};
    if(!parse_hex_color(spec, c))
    {
      // if invalid color, treat as plain text
      push_plain(in.substr(open, close_bracket - open + 1));
      i = close_bracket + 1;
      continue;
    }

    // find closing tag
    constexpr std::string_view end_tag = "[/color]";
    size_t end = in.find(end_tag, close_bracket + 1);
    if(end == std::string_view::npos)
    {
      // no closing tag -> treat everything as plain
      push_plain(in.substr(open));
      break;
    }

    // content inside
    std::string_view content = in.substr(close_bracket + 1, end - (close_bracket + 1));
    out.push_back({std::string(content), true, c});

    i = end + end_tag.size();
  }

  return out;
}

// Renderer: derive from imgui_md and override behavior.
struct MyMarkdown : public imgui_md
{
  static void compact_newline(float tighten = 6.0f)
  {
    ImGui::NewLine();
    ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - tighten));
  }

  bool get_image(image_info &nfo) const override
  {
    if(m_href.empty()) return false;
    if(starts_with(m_href, "http://") || starts_with(m_href, "https://")) return false;

    auto it = g_image_cache.find(m_href);
    if(it == g_image_cache.end())
    {
      TextureRecord rec = load_texture_from_file(resolve_image_path(m_href));
      it = g_image_cache.emplace(m_href, rec).first;
    }

    if(!it->second.loaded || it->second.texture_id == 0) return false;

    const float max_w = std::floor(std::max(8.0f, g_render_width));
    ImVec2 draw_size = it->second.size;
    if(draw_size.x > max_w)
    {
      const float s = max_w / draw_size.x;
      draw_size.x = max_w;
      draw_size.y *= s;
    }
    draw_size.x = std::floor(draw_size.x);
    draw_size.y = std::floor(draw_size.y);

    nfo.texture_id = (ImTextureID)(uintptr_t)it->second.texture_id;
    nfo.size = draw_size;
    nfo.uv0 = ImVec2(0, 0);
    nfo.uv1 = ImVec2(1, 1);
    nfo.col_tint = ImVec4(1, 1, 1, 1);
    nfo.col_border = ImVec4(0, 0, 0, 0);
    return true;
  }

  void BLOCK_P(bool e) override
  {
    // Extra compact paragraph spacing for dense note-taking.
    if(!e) ImGui::Dummy(ImVec2(0.0f, 0.0f));
  }

  void soft_break() override
  {
    // Compact line breaks from source markdown.
    compact_newline(7.0f);
  }

  ImFont *get_font() const override
  {
    // imgui_md tracks current state for you:
    // m_is_em, m_is_strong, m_is_table_header, m_hlevel, etc.
    // See imgui_md.h. :contentReference[oaicite:1]{index=1}
    if(m_is_table_header)
    {
      return g_fonts.bold ? g_fonts.bold : ImGui::GetFont();
    }

    // Headings: make them bold (optionally bigger if you have a bigger font).
    if(m_hlevel > 0)
    {
      return g_fonts.bold ? g_fonts.bold : ImGui::GetFont();
    }

    // Strong -> bold
    if(m_is_strong)
    {
      return g_fonts.bold ? g_fonts.bold : ImGui::GetFont();
    }

    // Emphasis -> italic
    if(m_is_em)
    {
      return g_fonts.italic ? g_fonts.italic : ImGui::GetFont();
    }

    // Normal
    return g_fonts.regular ? g_fonts.regular : ImGui::GetFont();
  }

  ImVec4 get_color() const override
  {
    const ImGuiStyle &st = ImGui::GetStyle();

    // Link: m_href is non-empty when rendering link text
    if(!m_href.empty())
      return st.Colors[ImGuiCol_ButtonHovered]; // pick any style color you like

    // Inline code: usually make it a bit “warm” or distinct
    if(m_is_code)
      return ImVec4(0.90f, 0.80f, 0.55f, 1.0f);

    // Headings: tint by level
    if(m_hlevel >= 1)
      return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);

    // Emphasis: keep normal text color (don’t gray it out)
    if(m_is_em)
      return st.Colors[ImGuiCol_Text];

    // Strong: also normal (or slightly brighter)
    if(m_is_strong)
      return st.Colors[ImGuiCol_Text];

    // Default
    return st.Colors[ImGuiCol_Text];
  }

  void open_url() const override
  {
#if defined(__EMSCRIPTEN__)
    // no-op or use JS bridge
#else
    if(!m_href.empty())
      SDL_OpenURL(m_href.c_str());
#endif
  }
};

static MyMarkdown &renderer()
{
  static MyMarkdown md;
  return md;
}

} // namespace

void MarkdownView::set_fonts(ImFont *regular, ImFont *italic, ImFont *bold)
{
  g_fonts = {regular, italic, bold};
}

void MarkdownView::set_render_width(float width)
{
  g_render_width = width > 0.0f ? width : 0.0f;
}
static void render_sections(const MdSection &s,
                            const std::function<void(std::string_view)> &render_body,
                            int &id_counter)
{
  // Render body before children (so text under a heading appears when expanded)
  // For root (level 0), we don't create a header; just render its body then children.
  if(s.level == 0)
  {
    if(!s.body.empty()) render_body(s.body);
    for(const auto &k : s.kids) render_sections(k, render_body, id_counter);
    return;
  }

  ImGui::PushID(id_counter++);

  // Collapsing header label (use the title only)
  const bool open = ImGui::CollapsingHeader(
      s.title.c_str(),
      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

  if(open)
  {
    if(!s.body.empty()) render_body(s.body);
    for(const auto &k : s.kids) render_sections(k, render_body, id_counter);
  }

  ImGui::PopID();
}

static void render_inline_md_with_color_spans(std::string_view text)
{
  auto &md = renderer();
  auto render_code_chip = [&](std::string_view code, bool colored, ImVec4 color) {
    if(code.empty()) return;
    const ImVec2 pad(4.0f, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(code.data(), code.data() + code.size());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);
    ImGui::Dummy(sz);

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    bg.w = 1.0f;
    ImU32 bg_col = ImGui::GetColorU32(bg);
    ImU32 text_col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), bg_col, 3.0f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), text_col, code.data(), code.data() + code.size());
  };

  auto render_md_fragment = [&](std::string_view frag, bool colored, ImVec4 color) {
    if(frag.empty()) return;
    if(colored) ImGui::PushStyleColor(ImGuiCol_Text, color);
    md.print(frag.data(), frag.data() + frag.size());
    if(colored) ImGui::PopStyleColor();
  };
  struct InlineToken
  {
    std::string text;
    bool colored = false;
    ImVec4 color{};
    bool newline = false;
  };

  std::vector<InlineToken> tokens;
  for(auto &seg : split_color_spans(text))
  {
    size_t start = 0;
    while(start <= seg.text.size())
    {
      size_t nl = seg.text.find('\n', start);
      const bool has_nl = (nl != std::string::npos);
      const size_t end = has_nl ? nl : seg.text.size();
      if(end > start)
      {
        tokens.push_back(InlineToken{
            seg.text.substr(start, end - start),
            seg.colored,
            seg.color,
            false});
      }
      if(has_nl) tokens.push_back(InlineToken{"", false, ImVec4{}, true});
      if(!has_nl) break;
      start = nl + 1;
    }
  }

  for(size_t i = 0; i < tokens.size(); ++i)
  {
    const auto &t = tokens[i];
      if(t.newline)
      {
        MyMarkdown::compact_newline(7.0f);
        continue;
      }

      size_t p = 0;
      bool rendered_any = false;
      while(p < t.text.size())
      {
        size_t bq0 = t.text.find('`', p);
        if(bq0 == std::string::npos)
        {
          render_md_fragment(std::string_view(t.text).substr(p), t.colored, t.color);
          rendered_any = true;
          break;
        }

        render_md_fragment(std::string_view(t.text).substr(p, bq0 - p), t.colored, t.color);
        rendered_any = true;

        size_t bq1 = t.text.find('`', bq0 + 1);
        if(bq1 == std::string::npos)
        {
          render_md_fragment(std::string_view(t.text).substr(bq0), t.colored, t.color);
          break;
        }

        ImGui::SameLine(0.0f, 0.0f);
        render_code_chip(std::string_view(t.text).substr(bq0 + 1, bq1 - bq0 - 1), t.colored, t.color);
        p = bq1 + 1;
        if(p < t.text.size()) ImGui::SameLine(0.0f, 0.0f);
      }

      const bool has_next = (i + 1 < tokens.size());
      if(rendered_any && has_next && !tokens[i + 1].newline) ImGui::SameLine(0.0f, 0.0f);
    }
}

void MarkdownView::render_inline(std::string_view markdown_inline)
{
  render_inline_md_with_color_spans(markdown_inline);
}

void MarkdownView::render(std::string_view markdown)
{
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
  // Helper: render any markdown text with your [color=#...] spans support
  auto render_with_color_spans = [&](std::string_view text) { render_inline_md_with_color_spans(text); };

  // Split into (normal markdown) and (note blocks). You already have this helper from earlier:
  // std::vector<Chunk> split_note_blocks(std::string_view)
  int note_idx = 0;
  for(const Chunk &c : split_note_blocks(markdown))
  {
    if(!c.is_note)
    {
      const MdSection root = parse_sections(c.text);
      int ids = 0;
      render_sections(root, render_with_color_spans, ids);
      continue;
    }

    // Render NOTE block as a card
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    bg.w = 0.18f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImVec4 bcol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    bcol.w = 0.75f;
    ImGui::PushStyleColor(ImGuiCol_Border, bcol);

    // Unique ID per note in this render call (no unbounded growth)
    ImGui::PushID(note_idx++);

    // --- Auto-height note card (no BeginChild) ---
    const float rounding = 8.0f;
    const ImVec2 pad(12.0f, 10.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);

    ImGui::BeginGroup();

    // Left padding inside the card
    ImGui::Dummy(ImVec2(pad.x, 0.0f));
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginGroup();

    // Render markdown content (advances cursor)
    render_with_color_spans(c.text);

    ImGui::EndGroup();
    ImGui::EndGroup();

    ImGui::PopStyleVar(); // WindowPadding (pad)

    // Compute the rectangle that encloses the just-rendered groups
    ImVec2 rect_min = ImGui::GetItemRectMin();
    ImVec2 rect_max = ImGui::GetItemRectMax();

    ImU32 bg_col = ImGui::GetColorU32(bg);
    ImU32 border_col = ImGui::GetColorU32(bcol);

    auto *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rect_min, rect_max, bg_col, rounding);
    dl->AddRect(rect_min, rect_max, border_col, rounding);
    dl->AddRectFilled(rect_min, ImVec2(rect_min.x + 5.0f, rect_max.y), ImGui::GetColorU32(ImGuiCol_ButtonHovered), rounding);

    // Right click on note block -> copy only this note block text
    ImGui::SetCursorScreenPos(rect_min);
    ImGui::InvisibleButton("##note_block_ctx", ImVec2(rect_max.x - rect_min.x, rect_max.y - rect_min.y));
    if(ImGui::BeginPopupContextItem("##note_block_popup", ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem("Copy note block"))
      {
        std::string plain;
        plain.reserve(c.text.size());
        size_t p = 0;
        while(p < c.text.size())
        {
          size_t e = c.text.find('\n', p);
          if(e == std::string::npos) e = c.text.size();
          std::string_view line(c.text.data() + p, e - p);
          std::string_view t = ltrim(line);
          if(starts_with(t, ">"))
          {
            t.remove_prefix(1);
            if(!t.empty() && t.front() == ' ') t.remove_prefix(1);
            plain.append(t.data(), t.size());
          }
          else
          {
            plain.append(line.data(), line.size());
          }
          if(e < c.text.size()) plain.push_back('\n');
          p = (e < c.text.size()) ? e + 1 : e;
        }
        ImGui::SetClipboardText(plain.c_str());
      }
      ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(rect_min.x, rect_max.y));

    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    ImGui::PopID();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::Dummy(ImVec2(0.0f, 0.0f));
  }
  ImGui::PopStyleVar();
}
