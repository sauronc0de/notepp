#include "markdown_view.hpp"

#include <SDL.h>
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

struct MdFonts
{
  ImFont *regular{};
  ImFont *italic{};
  ImFont *bold{};
};

static MdFonts g_fonts{};

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

void MarkdownView::render(std::string_view markdown)
{
  auto &md = renderer(); // your imgui_md derived renderer

  for(auto &seg : split_color_spans(markdown))
  {
    if(!seg.colored)
    {
      md.print(seg.text.data(), seg.text.data() + seg.text.size());
    }
    else
    {
      ImGui::PushStyleColor(ImGuiCol_Text, seg.color);
      md.print(seg.text.data(), seg.text.data() + seg.text.size());
      ImGui::PopStyleColor();
    }
  }
}
