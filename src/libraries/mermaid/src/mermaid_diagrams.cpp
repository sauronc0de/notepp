#include "mermaid_diagrams.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <imgui.h>

static constexpr float kPi = 3.14159265f;

namespace MermaidDiagrams
{
namespace
{
static ImVec2 nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

// ── parsing helpers ───────────────────────────────────────────────────────────
static bool sw(std::string_view s, std::string_view p) { return StringUtils::starts_with(s, p); }
static std::string_view tr(std::string_view s) { return StringUtils::trim(s); }
static std::string lc(std::string_view s) { return StringUtils::to_lower_copy(s); }

static std::string strip_quotes(std::string_view s)
{
  s = tr(s);
  if(s.size() >= 2 && ((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\'')))
    return std::string(s.substr(1, s.size()-2));
  return std::string(s);
}

static size_t find_unquoted(std::string_view s, char ch, size_t start = 0)
{
  char quote = 0;
  for(size_t i = start; i < s.size(); ++i) {
    const char c = s[i];
    if(quote) {
      if(c == quote) quote = 0;
    } else if(c == '"' || c == '\'') {
      quote = c;
    } else if(c == ch) {
      return i;
    }
  }
  return std::string_view::npos;
}

static std::vector<std::string> split_csv_items(std::string_view s)
{
  std::vector<std::string> items;
  size_t start = 0;
  char quote = 0;
  for(size_t i = 0; i <= s.size(); ++i) {
    const char c = i < s.size() ? s[i] : ',';
    if(i < s.size()) {
      if(quote) {
        if(c == quote) quote = 0;
      } else if(c == '"' || c == '\'') {
        quote = c;
      } else if(c != ',') {
        continue;
      }
    }

    std::string item = strip_quotes(tr(s.substr(start, i - start)));
    if(!item.empty()) items.push_back(std::move(item));
    start = i + 1;
  }
  return items;
}

static std::string_view strip_leading_quoted_label(std::string_view s)
{
  s = tr(s);
  if(s.size() < 2 || (s.front() != '"' && s.front() != '\'')) return s;
  const char quote = s.front();
  const size_t close = s.find(quote, 1);
  if(close == std::string_view::npos) return s;
  return tr(s.substr(close + 1));
}

static bool parse_bool_value(std::string_view s, bool defv)
{
  const std::string v = lc(tr(s));
  if(v == "true") return true;
  if(v == "false") return false;
  return defv;
}

static float parse_float_value(std::string_view s, float defv)
{
  std::string v(strip_quotes(tr(s)));
  char *end = nullptr;
  const float parsed = std::strtof(v.c_str(), &end);
  return end != v.c_str() ? parsed : defv;
}

static int count_leading_spaces(std::string_view s)
{
  int n = 0;
  while(n < (int)s.size() && s[(size_t)n] == ' ') ++n;
  return n;
}

static bool split_yaml_pair(std::string_view line, std::string_view &key, std::string_view &value)
{
  const size_t col = line.find(':');
  if(col == std::string_view::npos) return false;
  key = tr(line.substr(0, col));
  value = tr(line.substr(col + 1));
  return !key.empty();
}

static std::string parse_leading_label(std::string_view &s)
{
  s = tr(s);
  if(s.empty() || s.front() == '[') return {};
  if(s.front() == '"' || s.front() == '\'') {
    const char quote = s.front();
    const size_t close = s.find(quote, 1);
    if(close == std::string_view::npos) return {};
    std::string label = strip_quotes(s.substr(0, close + 1));
    s = tr(s.substr(close + 1));
    return label;
  }

  const size_t br = find_unquoted(s, '[');
  const size_t ar = s.find("-->");
  if(ar != std::string_view::npos && (br == std::string_view::npos || ar < br)) {
    std::string_view lhs = tr(s.substr(0, ar));
    const size_t sp = lhs.find_last_of(" \t");
    if(sp == std::string_view::npos) return {};
    std::string label = strip_quotes(tr(lhs.substr(0, sp)));
    s = tr(s.substr(sp + 1));
    return label;
  }

  size_t end = br == std::string_view::npos ? s.size() : br;
  std::string label = strip_quotes(tr(s.substr(0, end)));
  s = tr(s.substr(end));
  return label;
}

// iterate lines, trimmed, skipping empty, %% and // comments
struct Lines {
  std::string_view src;
  size_t pos = 0;
  bool next(std::string_view &out) {
    while(pos < src.size()) {
      size_t e = src.find('\n', pos);
      if(e == std::string_view::npos) e = src.size();
      std::string_view line = tr(src.substr(pos, e - pos));
      pos = (e < src.size()) ? e + 1 : e;
      if(line.empty() || sw(line, "%%") || sw(line, "//")) continue;
      out = line;
      return true;
    }
    return false;
  }
};

static bool read_bracket_list(Lines &lines, std::string_view first, std::string &inner)
{
  inner.clear();
  const size_t open = find_unquoted(first, '[');
  if(open == std::string_view::npos) return false;

  std::string_view rest = first.substr(open + 1);
  while(true) {
    const size_t close = find_unquoted(rest, ']');
    if(close != std::string_view::npos) {
      inner += std::string(rest.substr(0, close));
      return true;
    }

    inner += std::string(rest);
    std::string_view next;
    if(!lines.next(next)) return false;
    inner += '\n';
    rest = next;
  }
}

// split "A --> B : label" at first occurrence of any arrow
static bool split_arrow(std::string_view line, std::string_view &lhs,
                         std::string_view &rhs, std::string &label_out)
{
  static const char *arrows[] = {"-->>","--x","-->","-.->","<-->>","<-->","<--","-.","---","->","==>",nullptr};
  size_t best = std::string_view::npos, best_len = 0;
  for(int i = 0; arrows[i]; ++i) {
    size_t p = line.find(arrows[i]);
    if(p != std::string_view::npos && (best == std::string_view::npos || p < best)) {
      best = p; best_len = std::strlen(arrows[i]);
    }
  }
  if(best == std::string_view::npos) return false;
  lhs = tr(line.substr(0, best));
  std::string_view r = tr(line.substr(best + best_len));
  // check label after ":"
  size_t col = r.rfind(':');
  if(col != std::string_view::npos && col+1 < r.size()) {
    label_out = std::string(tr(r.substr(col+1)));
    rhs = tr(r.substr(0, col));
  } else {
    rhs = r; label_out = "";
  }
  return !lhs.empty() && !rhs.empty();
}

// drawing helpers
static ImVec2 center_text(ImVec2 p, ImVec2 sz, const std::string &t)
{
  ImVec2 ts = ImGui::CalcTextSize(t.c_str());
  return ImVec2(p.x + (sz.x - ts.x)*0.5f, p.y + (sz.y - ts.y)*0.5f);
}

static void draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open=false)
{
  ImVec2 n(-dir.y, dir.x);
  ImVec2 l(tip.x - dir.x*sz + n.x*(sz*0.5f), tip.y - dir.y*sz + n.y*(sz*0.5f));
  ImVec2 r(tip.x - dir.x*sz - n.x*(sz*0.5f), tip.y - dir.y*sz - n.y*(sz*0.5f));
  if(open) { dl->AddLine(l, tip, col, 1.5f); dl->AddLine(r, tip, col, 1.5f); }
  else      dl->AddTriangleFilled(tip, l, r, col);
}

static ImU32 series_color(int i, float alpha=1.0f)
{
  float h = static_cast<float>(i) * 0.618034f; h -= std::floor(h);
  float r,g,b; ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r,g,b,alpha));
}

// layout: place n items in rows of w, return {col, row} for index i
static std::pair<int,int> grid_pos(int i, int cols)
{ return {i%cols, i/cols}; }

// wrap label to max_w pixels, returns multi-line string
static std::string wrap_label(const std::string &s, float max_w)
{
  if(ImGui::CalcTextSize(s.c_str()).x <= max_w) return s;
  std::istringstream ss(s);
  std::string word, line, result;
  while(ss >> word) {
    std::string test = line.empty() ? word : line+" "+word;
    if(ImGui::CalcTextSize(test.c_str()).x <= max_w) { line = test; }
    else { if(!result.empty()) result+="\n"; result+=line; line=word; }
  }
  if(!line.empty()) { if(!result.empty()) result+="\n"; result+=line; }
  return result;
}

// draw a rounded box and return the rect painted
static ImVec4 draw_box(ImDrawList *dl, ImVec2 p, ImVec2 sz, ImU32 fill,
                        ImU32 border, const std::string &label, float rounding=4.0f)
{
  ImVec2 p2(p.x+sz.x, p.y+sz.y);
  dl->AddRectFilled(p, p2, fill, rounding);
  dl->AddRect(p, p2, border, rounding);
  ImVec2 tp = center_text(p, sz, label);
  dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
  return ImVec4(p.x, p.y, p2.x, p2.y);
}

// like Lines but preserves raw indentation; returns trimmed text + indent count
struct IndentLines {
  std::string_view src; size_t pos=0;
  bool next(std::string_view &out, int &indent) {
    while(pos < src.size()) {
      size_t e=src.find('\n',pos);
      if(e==std::string_view::npos) e=src.size();
      std::string_view raw=src.substr(pos,e-pos);
      pos=(e<src.size())?e+1:e;
      std::string_view t=tr(raw);
      if(t.empty()||sw(t,"%%")) continue;
      indent=0; for(char c:raw){if(c==' ')indent++;else if(c=='\t')indent+=2;else break;}
      out=t; return true;
    }
    return false;
  }
};

// point on box boundary (half-extents hw,hh) in direction of 'other'
static ImVec2 rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  float dx=other.x-cen.x, dy=other.y-cen.y;
  if(std::abs(dx)<0.001f&&std::abs(dy)<0.001f) return cen;
  float tx=hw/std::abs(dx), ty=hh/std::abs(dy);
  float t=std::min(tx,ty);
  return ImVec2(cen.x+dx*t, cen.y+dy*t);
}

// point on circle boundary of radius r toward 'other'
static ImVec2 circ_edge(ImVec2 cen, float r, ImVec2 other)
{
  float dx=other.x-cen.x, dy=other.y-cen.y;
  float len=std::sqrt(dx*dx+dy*dy);
  if(len<0.001f) return cen;
  return ImVec2(cen.x+dx/len*r, cen.y+dy/len*r);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// SEQUENCE DIAGRAM  (see src/diagrams/sequence_parser.cpp / sequence_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// CLASS DIAGRAM      (see src/diagrams/class_parser.cpp / class_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// STATE DIAGRAM      (see src/diagrams/state_parser.cpp / state_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// ER DIAGRAM         (see src/diagrams/er_parser.cpp    / er_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// USER JOURNEY       (see src/diagrams/journey_parser.cpp / journey_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// GANTT             (see src/diagrams/gantt_parser.cpp / gantt_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// QUADRANT CHART    (see src/diagrams/quadrant_parser.cpp / quadrant_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// REQUIREMENT DIAGRAM (see src/diagrams/requirement_parser.cpp / requirement_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// GIT GRAPH  (see src/diagrams/git_parser.cpp / git_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// MINDMAP    (see src/diagrams/mindmap_parser.cpp / mindmap_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// TIMELINE   (see src/diagrams/timeline_parser.cpp / timeline_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// SANKEY     (see src/diagrams/sankey_parser.cpp / sankey_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// BLOCK DIAGRAM (see src/diagrams/block_parser.cpp / block_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// PACKET DIAGRAM (see src/diagrams/packet_parser.cpp / packet_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// KANBAN              (see src/diagrams/kanban_parser.cpp / kanban_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// ARCHITECTURE        (see src/diagrams/architecture_parser.cpp / architecture_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// RADAR CHART         (see src/diagrams/radar_parser.cpp / radar_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// TREEMAP             (see src/diagrams/treemap_parser.cpp / treemap_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// ZENUML              (see src/diagrams/sequence_parser.cpp / sequence_renderer.cpp - share SequenceDiagram)
// ═══════════════════════════════════════════════════════════════════════════
// EVENT MODELING      (see src/diagrams/eventmodeling_parser.cpp / eventmodeling_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// VENN                (see src/diagrams/venn_parser.cpp / venn_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// ISHIKAWA            (see src/diagrams/ishikawa_parser.cpp / ishikawa_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// WARDLEY MAP         (see src/diagrams/wardley_parser.cpp / wardley_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════
// TREEVIEW            (see src/diagrams/treeview_parser.cpp / treeview_renderer.cpp)
// ═══════════════════════════════════════════════════════════════════════════

} // namespace MermaidDiagrams

// ── Interactive edit back-channel ──────────────────────────────────────────
// render_* functions write here when the user edits a diagram interactively.
// render_preview_with_task_checkboxes_ex reads and applies it to the markdown.
namespace MermaidDiagrams
{
PendingEdit g_pending_edit;
bool g_consumed_right_click = false;
} // namespace MermaidDiagrams

