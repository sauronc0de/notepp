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
// ═══════════════════════════════════════════════════════════════════════════
// PACKET
// ═══════════════════════════════════════════════════════════════════════════

// Parses %%{init: {'packet': {'bitWidth':20,'bitsPerRow':16,...}}}%% directives.
static void parse_packet_config(std::string_view src, PacketConfig &cfg)
{
  const std::string s(src);
  size_t pos = 0;
  while ((pos = s.find("%%{", pos)) != std::string::npos) {
    size_t end = s.find("%%", pos + 3);
    if (end == std::string::npos) break;
    const std::string dir = s.substr(pos + 3, end - pos - 3);
    size_t pk = dir.find("packet");
    if (pk != std::string::npos) {
      size_t colon = dir.find(':', pk + 6);
      size_t ob    = (colon != std::string::npos) ? dir.find('{', colon) : std::string::npos;
      size_t cb    = (ob != std::string::npos) ? dir.find('}', ob + 1) : std::string::npos;
      if (cb != std::string::npos) {
        const std::string inner = dir.substr(ob + 1, cb - ob - 1);
        auto gf = [&](const char *k, float &v) {
          size_t kp = inner.find(k); if (kp == std::string::npos) return;
          size_t cp = inner.find(':', kp + std::strlen(k)); if (cp == std::string::npos) return;
          float r = (float)std::atof(inner.c_str() + cp + 1); if (r > 0) v = r;
        };
        auto gi = [&](const char *k, int &v) {
          size_t kp = inner.find(k); if (kp == std::string::npos) return;
          size_t cp = inner.find(':', kp + std::strlen(k)); if (cp == std::string::npos) return;
          int r = std::atoi(inner.c_str() + cp + 1); if (r > 0) v = r;
        };
        auto gb = [&](const char *k, bool &v) {
          size_t kp = inner.find(k); if (kp == std::string::npos) return;
          size_t cp = inner.find(':', kp + std::strlen(k)); if (cp == std::string::npos) return;
          const std::string rest = inner.substr(cp + 1);
          size_t tp = rest.find("true"), fp2 = rest.find("false");
          if (tp != std::string::npos && (fp2 == std::string::npos || tp < fp2)) v = true;
          else if (fp2 != std::string::npos) v = false;
        };
        gf("bitWidth",   cfg.bitWidth);
        gf("rowHeight",  cfg.rowHeight);
        gi("bitsPerRow", cfg.bitsPerRow);
        gb("showBits",   cfg.showBits);
        gf("paddingX",   cfg.paddingX);
        gf("paddingY",   cfg.paddingY);
        gb("showLegend", cfg.showLegend);
      }
    }
    pos = end + 2;
  }
}

bool parse_packet(std::string_view src, PacketDiagram &out)
{
  out = PacketDiagram{};
  parse_packet_config(src, out.config);
  Lines L{src}; std::string_view line; bool header = false;
  int cur = 0;  // next available bit — used by implicit-start "+N" notation
  while (L.next(line)) {
    std::string ll = lc(line);
    if (!header) { if (sw(ll,"packet-beta") || sw(ll,"packet")) { header=true; continue; } continue; }
    if (sw(ll,"%%") || sw(ll,"//")) continue;  // skip comments and directives
    if (sw(ll,"title ")) { out.title = strip_quotes(line.substr(6)); continue; }
    size_t col = line.find(':'); if (col == std::string_view::npos) continue;
    std::string range_s = std::string(tr(line.substr(0, col)));
    std::string name    = strip_quotes(line.substr(col + 1));
    int start = 0, end = 0;
    size_t dash = range_s.find('-'), plus = range_s.find('+');
    if (dash != std::string::npos) {
      // "A-B" — explicit range
      start = std::atoi(range_s.substr(0, dash).c_str());
      end   = std::atoi(range_s.substr(dash + 1).c_str());
    } else if (plus == 0) {
      // "+N" — implicit start: begin at current position, length N
      start = cur;
      end   = cur + std::atoi(range_s.c_str() + 1) - 1;
    } else if (plus != std::string::npos) {
      // "A+N" — explicit start with length
      start = std::atoi(range_s.substr(0, plus).c_str());
      end   = start + std::atoi(range_s.substr(plus + 1).c_str()) - 1;
    } else {
      // "N" — single bit
      start = end = std::atoi(range_s.c_str());
    }
    cur = end + 1;
    out.total_bits = std::max(out.total_bits, end + 1);
    out.fields.push_back({start, end, name});
  }
  return header && !out.fields.empty();
}

// ── Interactive packet diagram helpers ───────────────────────────────────────

PendingEdit g_pending_edit;
bool        g_consumed_right_click = false;

static std::string serialize_packet(const PacketDiagram &d)
{
  const PacketConfig &c = d.config;
  std::ostringstream s;
  s << "%%{init: {'packet': {"
    << "'bitWidth': "    << (int)c.bitWidth
    << ", 'rowHeight': " << (int)c.rowHeight
    << ", 'bitsPerRow': "<< c.bitsPerRow
    << ", 'showBits': "  << (c.showBits   ? "true" : "false")
    << ", 'paddingX': "  << (int)c.paddingX
    << ", 'paddingY': "  << (int)c.paddingY
    << ", 'showLegend': "<< (c.showLegend ? "true" : "false")
    << "}}}%%\n"
    << "packet-beta\n";
  if (!d.title.empty()) s << "  title \"" << d.title << "\"\n";
  int cur = 0;
  for (auto &f : d.fields) {
    int bits = std::max(1, f.end - f.start + 1);
    s << "  " << cur << "-" << (cur + bits - 1) << ": \"" << f.name << "\"\n";
    cur += bits;
  }
  return s.str();
}

// Rebuild sequential start/end positions from each field's current bit count.
static void rebuild_bits(std::vector<PacketField> &fields)
{
  int cur = 0;
  for (auto &f : fields) {
    int bits = std::max(1, f.end - f.start + 1);
    f.start = cur; f.end = cur + bits - 1; cur = f.end + 1;
  }
}

struct PacketEditState {
  PacketConfig cfg_edit;                 // config being edited in the config popup
  bool         cfg_open     = false;
  int          ctx_fi       = -1;        // field right-clicked
  int          rename_fi    = -1;
  char         rename_buf[256] = {};
  bool         rename_focus = false;
  int          add_after    = -2;        // -2=closed, -1=prepend, >=0=insert after fi
  char         add_name[256] = {};
  int          add_bits     = 8;
  bool         add_focus    = false;
  bool         drag_active  = false;
  int          drag_fi      = -1;
  std::vector<PacketField> drag_fields;
  std::vector<int>         drag_colors; // original index per slot — keeps colors stable
  bool  resize_active    = false;
  int   resize_fi        = -1;
  int   resize_orig_bits = 0;
  float resize_start_x   = 0.0f;
};
static std::unordered_map<int, PacketEditState> s_pkt_states;

void render_packet(const PacketDiagram &d, int id)
{
  ImGui::PushID(id);
  auto &es = s_pkt_states[id];

  // Working fields — during drag/resize we show the live state
  const std::vector<PacketField> &wf = (es.drag_active || es.resize_active) ? es.drag_fields : d.fields;
  const int NF = (int)wf.size();

  const PacketConfig &cfg = d.config;
  const float bit_w = cfg.bitWidth, fh = cfg.rowHeight;
  const float px = cfg.paddingX, py = cfg.paddingY;
  const float hdr_h = cfg.showBits ? 16.0f : 0.0f;
  const float sq = 10.0f, lgap = 5.0f, igap = 10.0f;
  const float outer = 4.0f;

  int wf_total = 0;
  for (auto &f : wf) wf_total = std::max(wf_total, f.end + 1);
  int total  = std::max(std::max(wf_total, d.total_bits), 1);
  int bpr    = std::max(1, cfg.bitsPerRow);
  int n_rows = (total + bpr - 1) / bpr;
  float cw   = (float)bpr * bit_w + outer * 2.0f;

  // ── Per-field metadata ───────────────────────────────────────────────────
  std::vector<bool> ext(NF, false);
  std::vector<int>  label_row(NF, 0);
  for (int fi = 0; fi < NF; ++fi) {
    auto &f = wf[fi];
    int best_seg = 0, best_r = 0;
    for (int row = 0; row < n_rows; ++row) {
      int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs <= fe) { int seg = fe - fs + 1; if (seg > best_seg) { best_seg = seg; best_r = row; } }
    }
    label_row[fi] = best_r;
    ext[fi] = ImGui::CalcTextSize(f.name.c_str()).x > (float)best_seg * bit_w - px - 4.0f;
  }
  bool any_ext = false;
  for (bool b : ext) if (b) { any_ext = true; break; }

  float legend_h = 0.0f;
  if (any_ext && cfg.showLegend) {
    float avail_w = cw - outer * 2.0f, lx_s = 0.0f; int lrows = 1;
    for (int fi = 0; fi < NF; ++fi) {
      if (!ext[fi]) continue;
      float iw = sq + lgap + ImGui::CalcTextSize(wf[fi].name.c_str()).x + igap;
      if (lx_s + iw > avail_w && lx_s > 0.0f) { lx_s = 0.0f; lrows++; }
      lx_s += iw;
    }
    legend_h = py + (float)lrows * 18.0f;
  }

  const float title_h  = d.title.empty() ? 0.0f : (py * 0.5f + 16.0f);
  const float row_step = hdr_h + fh + py;
  const float ch = py * 0.5f + title_h + (float)n_rows * row_step + legend_h;

  // ── Origin + vis_rect (defined before InvisibleButton so frects can be built) ─
  const ImVec2 orig = ImGui::GetCursorScreenPos();

  // vis_rect: (x, width) of the visual (inset) rect for a field segment [fs,fe]
  // within row [rs,re]. paddingX/2 inset on each non-edge side.
  auto vis_rect = [&](int rs, int re, int fs, int fe) -> std::pair<float,float> {
    float li = (fs == rs) ? 0.0f : px * 0.5f;
    float ri = (fe == re) ? 0.0f : px * 0.5f;
    float vfx = orig.x + outer + (float)(fs - rs) * bit_w + li;
    float vfw = (float)(fe - fs + 1) * bit_w - li - ri;
    if (vfw < 1.0f) { vfx -= li; vfw += li + ri; }
    return {vfx, vfw};
  };

  // Pre-compute field rects for hit-testing (hover + drag + resize)
  struct FR { int fi, fe; float x, y, w; };
  std::vector<FR> frects;
  frects.reserve(NF * n_rows);
  {
    const float yb0 = orig.y + py * 0.5f + title_h;
    for (int row = 0; row < n_rows; ++row) {
      int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
      float fy0 = yb0 + (float)row * row_step + hdr_h;
      for (int fi = 0; fi < NF; ++fi) {
        int fs = std::max(wf[fi].start, rs), fe = std::min(wf[fi].end, re);
        if (fs > fe) continue;
        auto [vfx, vfw] = vis_rect(rs, re, fs, fe);
        frects.push_back({fi, fe, vfx, fy0, vfw});
      }
    }
  }

  // ── InvisibleButton ───────────────────────────────────────────────────────
  ImGui::InvisibleButton("##pkt", nonzero_invisible_button_size(cw, ch));
  const bool pkt_hovered = ImGui::IsItemHovered();
  const bool pkt_active  = ImGui::IsItemActive();

  // ── Hover + drag + resize detection ──────────────────────────────────────
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float edge_thresh = 6.0f;

  int hovered_fi = -1;
  for (auto &r : frects)
    if (mouse.x >= r.x && mouse.x < r.x + r.w && mouse.y >= r.y && mouse.y < r.y + fh)
      { hovered_fi = r.fi; break; }
  if (!pkt_hovered && !es.drag_active && !es.resize_active) hovered_fi = -1;

  // Detect right-edge hover (for resize cursor + resize drag start)
  int edge_fi = -1;
  if (!es.drag_active && !es.resize_active) {
    for (auto &r : frects) {
      if (r.fe != wf[r.fi].end) continue; // only last segment of the field
      float right = r.x + r.w;
      if (mouse.x >= right - edge_thresh && mouse.x <= right + edge_thresh &&
          mouse.y >= r.y && mouse.y < r.y + fh)
        { edge_fi = r.fi; break; }
    }
    if (!pkt_hovered) edge_fi = -1;
  }

  // Drag/resize start (require 5 px movement to distinguish from click)
  if (!es.drag_active && !es.resize_active && pkt_active && ImGui::IsMouseDragging(0, 5.0f)) {
    ImVec2 dp = ImGui::GetMouseDragDelta(0);
    ImVec2 pp = {mouse.x - dp.x, mouse.y - dp.y};
    // Check right-edge drag → resize
    bool started = false;
    for (auto &r : frects) {
      if (r.fe != wf[r.fi].end) continue;
      float right = r.x + r.w;
      if (pp.x >= right - edge_thresh && pp.x <= right + edge_thresh &&
          pp.y >= r.y && pp.y < r.y + fh) {
        es.resize_active    = true;
        es.resize_fi        = r.fi;
        es.resize_orig_bits = wf[r.fi].end - wf[r.fi].start + 1;
        es.resize_start_x   = pp.x;
        es.drag_fields      = d.fields;
        es.drag_colors.resize(d.fields.size());
        std::iota(es.drag_colors.begin(), es.drag_colors.end(), 0);
        started = true;
        break;
      }
    }
    // Fall back to field-swap drag
    if (!started) {
      for (auto &r : frects)
        if (pp.x >= r.x && pp.x < r.x + r.w && pp.y >= r.y && pp.y < r.y + fh)
          { es.drag_active = true; es.drag_fi = r.fi; es.drag_fields = d.fields;
            es.drag_colors.resize(d.fields.size()); std::iota(es.drag_colors.begin(), es.drag_colors.end(), 0); break; }
    }
  }

  // Resize update / end
  if (es.resize_active) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    int fi = es.resize_fi;
    float delta_x   = mouse.x - es.resize_start_x;
    int   delta_bits = (int)std::round(delta_x / bit_w);
    int   new_bits   = std::max(1, es.resize_orig_bits + delta_bits);
    // Rebuild drag_fields from d.fields with updated bit count for fi
    es.drag_fields = d.fields;
    if (fi < (int)es.drag_fields.size()) {
      int new_end = es.drag_fields[fi].start + new_bits - 1;
      int diff    = new_end - es.drag_fields[fi].end;
      es.drag_fields[fi].end = new_end;
      for (int k = fi + 1; k < (int)es.drag_fields.size(); ++k) {
        es.drag_fields[k].start += diff;
        es.drag_fields[k].end   += diff;
      }
    }
    if (!ImGui::IsMouseDown(0)) {
      if (new_bits != es.resize_orig_bits && fi < (int)d.fields.size()) {
        PacketDiagram nd = d; nd.fields = es.drag_fields;
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      es.resize_active = false; es.resize_fi = -1;
      es.drag_fields.clear(); es.drag_colors.clear();
    }
  }

  // Field-swap drag update / end
  if (es.drag_active) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (!ImGui::IsMouseDown(0)) {
      bool changed = false;
      for (int i = 0; i < (int)d.fields.size() && i < (int)es.drag_fields.size(); ++i)
        if (d.fields[i].name != es.drag_fields[i].name ||
            (d.fields[i].end - d.fields[i].start) != (es.drag_fields[i].end - es.drag_fields[i].start))
          { changed = true; break; }
      if (changed) {
        PacketDiagram nd = d; nd.fields = es.drag_fields;
        nd.total_bits = 0; for (auto &f : nd.fields) nd.total_bits = std::max(nd.total_bits, f.end + 1);
        g_pending_edit = {id, serialize_packet(nd)};
      }
      es.drag_active = false; es.drag_fi = -1; es.drag_fields.clear(); es.drag_colors.clear();
    } else if (hovered_fi >= 0 && hovered_fi != es.drag_fi &&
               hovered_fi < (int)es.drag_fields.size()) {
      std::swap(es.drag_fields[es.drag_fi], es.drag_fields[hovered_fi]);
      if (es.drag_fi < (int)es.drag_colors.size() && hovered_fi < (int)es.drag_colors.size())
        std::swap(es.drag_colors[es.drag_fi], es.drag_colors[hovered_fi]);
      rebuild_bits(es.drag_fields);
      es.drag_fi = hovered_fi;
    }
  } else if (es.resize_active) {
    // cursor already set above
  } else if (edge_fi >= 0) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  } else if (hovered_fi >= 0) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  // Stable color index: during drag, map current slot back to original index
  // so colors travel with the field, not with the slot.
  auto ci = [&](int fi) -> int {
    return (es.drag_active && fi < (int)es.drag_colors.size()) ? es.drag_colors[fi] : fi;
  };

  // ── Draw list + colors ────────────────────────────────────────────────────
  ImDrawList *dl   = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hcol = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
  ImVec4 lbg4 = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); lbg4.w = 0.88f;
  const ImU32 lbg = ImGui::GetColorU32(lbg4);

  float y_base = orig.y + py * 0.5f;

  // ── Title ─────────────────────────────────────────────────────────────────
  if (!d.title.empty()) {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, y_base), tcol, d.title.c_str());
    y_base += title_h;
  }

  // ── Rows ──────────────────────────────────────────────────────────────────
  for (int row = 0; row < n_rows; ++row) {
    int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
    float ry = y_base + (float)row * row_step;
    float fy = ry + hdr_h;

    auto vr = [&](int fs, int fe) { return vis_rect(rs, re, fs, fe); };

    // Bit-number header — x aligned with the evenly-spaced tick grid
    if (cfg.showBits) {
      auto bit_hdr_x = [&](int bit) -> float {
        for (int fi2 = 0; fi2 < NF; ++fi2) {
          int fs2 = std::max(wf[fi2].start, rs), fe2 = std::min(wf[fi2].end, re);
          if (bit >= fs2 && bit <= fe2) {
            auto [vfx2, vfw2] = vr(fs2, fe2);
            return vfx2 + (float)(bit - fs2) / (float)(fe2 - fs2 + 1) * vfw2;
          }
        }
        return orig.x + outer + (float)(bit - rs) * bit_w;
      };
      for (int i = rs; i <= re; i += 8) {
        char buf[8]; std::snprintf(buf, sizeof(buf), "%d", i);
        dl->AddText(ImVec2(bit_hdr_x(i), ry), lcol, buf);
      }
    }

    // Pass 1 — fills + borders + hover/drag highlight
    for (int fi = 0; fi < NF; ++fi) {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      dl->AddRectFilled(ImVec2(vfx, fy), ImVec2(vfx+vfw, fy+fh), series_color(ci(fi), 0.42f), 3.0f);
      dl->AddRect(ImVec2(vfx, fy), ImVec2(vfx+vfw, fy+fh), series_color(ci(fi), 0.80f), 3.0f, 0, 1.5f);
      if (fi == hovered_fi)
        dl->AddRect(ImVec2(vfx-1,fy-1), ImVec2(vfx+vfw+1,fy+fh+1), hcol, 4.0f, 0, 2.0f);
    }

    // Bit tick marks — evenly spaced within visual width so all bits are equal size
    for (int fi = 0; fi < NF; ++fi) {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      int nbits = fe - fs + 1;
      for (int j = 0; j <= nbits; ++j) {
        float tx = vfx + (float)j / (float)nbits * vfw;
        dl->AddLine(ImVec2(tx, fy), ImVec2(tx, fy+fh), bord, 0.5f);
      }
    }

    // Pass 2 — labels (top layer, drawn over ticks)
    for (int fi = 0; fi < NF; ++fi) {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if (fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      if (!ext[fi] && row == label_row[fi]) {
        ImVec2 ts = ImGui::CalcTextSize(f.name.c_str());
        float lx2 = vfx + (vfw - ts.x) * 0.5f, ly2 = fy + (fh - ts.y) * 0.5f;
        const float lp = 3.0f;
        dl->AddRectFilled(ImVec2(lx2-lp, ly2-lp), ImVec2(lx2+ts.x+lp, ly2+ts.y+lp), lbg, 2.5f);
        dl->AddText(ImVec2(lx2, ly2), tcol, f.name.c_str());
      } else if (ext[fi]) {
        dl->AddCircleFilled(ImVec2(vfx+vfw*0.5f, fy+fh-5.0f), 2.5f, series_color(ci(fi), 0.9f));
      }
    }
  }

  // ── Resize edge highlight ─────────────────────────────────────────────────
  {
    int edge_hl = es.resize_active ? es.resize_fi : edge_fi;
    if (edge_hl >= 0) {
      const ImU32 ecol = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
      for (auto &r : frects) {
        if (r.fi != edge_hl || r.fe != wf[r.fi].end) continue;
        float rx = r.x + r.w;
        dl->AddLine(ImVec2(rx, r.y - 1), ImVec2(rx, r.y + fh + 1), ecol, 2.5f);
        break;
      }
    }
  }

  // ── Legend ────────────────────────────────────────────────────────────────
  if (any_ext && cfg.showLegend) {
    float lx = orig.x + outer, ly = y_base + (float)n_rows * row_step + py;
    float right_edge = orig.x + cw - outer;
    for (int fi = 0; fi < NF; ++fi) {
      if (!ext[fi]) continue;
      float iw = sq + lgap + ImGui::CalcTextSize(wf[fi].name.c_str()).x + igap;
      if (lx + iw > right_edge && lx > orig.x + outer) { lx = orig.x + outer; ly += 18.0f; }
      dl->AddRectFilled(ImVec2(lx, ly+2), ImVec2(lx+sq, ly+sq+2), series_color(ci(fi), 0.85f), 2.0f);
      dl->AddText(ImVec2(lx+sq+lgap, ly), tcol, wf[fi].name.c_str());
      lx += iw;
    }
  }

  // ── Hover tooltip ─────────────────────────────────────────────────────────
  if (es.resize_active) {
    int fi = es.resize_fi;
    if (fi < (int)es.drag_fields.size()) {
      const auto &hf = es.drag_fields[fi];
      int nbits = hf.end - hf.start + 1;
      ImGui::SetTooltip("%s\n%db", hf.name.c_str(), nbits);
    }
  } else if (pkt_hovered && hovered_fi >= 0 && !es.drag_active) {
    const auto &hf = wf[hovered_fi];
    int nbits = hf.end - hf.start + 1;
    ImGui::SetTooltip("%s\n[%d-%d]  %db", hf.name.c_str(), hf.start, hf.end, nbits);
  }

  // ── Context menu trigger (right-click) ────────────────────────────────────
  bool open_rename = false, open_add = false;
  if (pkt_hovered && !es.drag_active && !es.resize_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    g_consumed_right_click = true;
    if (hovered_fi >= 0) {
      es.ctx_fi = hovered_fi;
      ImGui::OpenPopup("##pkt_ctx");
    } else {
      es.cfg_edit = d.config;
      ImGui::OpenPopup("##pkt_cfg");
    }
  }

  // ── Field context menu ────────────────────────────────────────────────────
  if (ImGui::BeginPopup("##pkt_ctx")) {
    const int fi = es.ctx_fi;
    if (fi >= 0 && fi < (int)d.fields.size()) {
      char hdr[128];
      std::snprintf(hdr, sizeof(hdr), "%s  [%d-%d]  %db",
                    d.fields[fi].name.c_str(), d.fields[fi].start, d.fields[fi].end,
                    d.fields[fi].end - d.fields[fi].start + 1);
      ImGui::TextDisabled("%s", hdr);
      ImGui::Separator();
      if (ImGui::MenuItem("Rename...")) {
        es.rename_fi = fi;
        std::strncpy(es.rename_buf, d.fields[fi].name.c_str(), 255);
        es.rename_buf[255] = '\0';
        es.rename_focus = true;
        open_rename = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Expand +1 bit")) {
        PacketDiagram nd = d;
        nd.fields[fi].end++;
        for (int k = fi+1; k < (int)nd.fields.size(); ++k) { nd.fields[k].start++; nd.fields[k].end++; }
        nd.total_bits++;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      bool can_shrink = d.fields[fi].end > d.fields[fi].start;
      if (ImGui::MenuItem("Shrink -1 bit", nullptr, false, can_shrink)) {
        PacketDiagram nd = d;
        nd.fields[fi].end--;
        for (int k = fi+1; k < (int)nd.fields.size(); ++k) { nd.fields[k].start--; nd.fields[k].end--; }
        nd.total_bits--;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Add field before")) {
        es.add_after = fi - 1; es.add_bits = 8; es.add_name[0] = '\0'; es.add_focus = true;
        open_add = true;
      }
      if (ImGui::MenuItem("Add field after")) {
        es.add_after = fi; es.add_bits = 8; es.add_name[0] = '\0'; es.add_focus = true;
        open_add = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Delete field")) {
        PacketDiagram nd = d;
        nd.fields.erase(nd.fields.begin() + fi);
        rebuild_bits(nd.fields);
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
    }
    ImGui::EndPopup();
  }
  // Open these AFTER EndPopup so they don't nest inside the context menu
  if (open_rename) ImGui::OpenPopup("##pkt_rename");
  if (open_add)    ImGui::OpenPopup("##pkt_add");

  // ── Config popup ──────────────────────────────────────────────────────────
  if (ImGui::BeginPopup("##pkt_cfg")) {
    ImGui::Text("Packet Config");
    ImGui::Separator();
    ImGui::SliderFloat("Bit Width",       &es.cfg_edit.bitWidth,   8.f, 80.f,  "%.0fpx");
    ImGui::SliderFloat("Row Height",      &es.cfg_edit.rowHeight,  16.f,120.f, "%.0fpx");
    ImGui::SliderInt  ("Bits / Row",      &es.cfg_edit.bitsPerRow, 4, 128);
    ImGui::SliderFloat("Padding X",       &es.cfg_edit.paddingX,   0.f, 40.f,  "%.0fpx");
    ImGui::SliderFloat("Padding Y",       &es.cfg_edit.paddingY,   0.f, 40.f,  "%.0fpx");
    ImGui::Checkbox   ("Show bit numbers",&es.cfg_edit.showBits);
    ImGui::Checkbox   ("Show legend",     &es.cfg_edit.showLegend);
    ImGui::Separator();
    if (ImGui::Button("Apply")) {
      PacketDiagram nd = d; nd.config = es.cfg_edit;
      g_pending_edit = {id, serialize_packet(nd)};
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Rename modal ──────────────────────────────────────────────────────────
  if (ImGui::BeginPopupModal("##pkt_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Rename field:");
    if (es.rename_focus) { ImGui::SetKeyboardFocusHere(); es.rename_focus = false; }
    bool ok = ImGui::InputText("##rn", es.rename_buf, sizeof(es.rename_buf),
                               ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (ImGui::Button("OK") || ok) {
      if (es.rename_fi >= 0 && es.rename_fi < (int)d.fields.size() && es.rename_buf[0]) {
        PacketDiagram nd = d;
        nd.fields[es.rename_fi].name = es.rename_buf;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Add field modal ───────────────────────────────────────────────────────
  if (ImGui::BeginPopupModal("##pkt_add", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("New field:");
    if (es.add_focus) { ImGui::SetKeyboardFocusHere(); es.add_focus = false; }
    ImGui::InputText("Name##an", es.add_name, sizeof(es.add_name));
    ImGui::InputInt ("Bits##ab", &es.add_bits);
    if (es.add_bits < 1) es.add_bits = 1;
    ImGui::Spacing();
    if (ImGui::Button("Add")) {
      if (es.add_name[0]) {
        PacketDiagram nd = d;
        PacketField nf; nf.name = es.add_name; nf.start = 0; nf.end = es.add_bits - 1;
        int ins = std::max(0, std::min(es.add_after + 1, (int)nd.fields.size()));
        nd.fields.insert(nd.fields.begin() + ins, nf);
        rebuild_bits(nd.fields);
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// KANBAN
// ═══════════════════════════════════════════════════════════════════════════
bool parse_kanban(std::string_view src, KanbanDiagram &out)
{
  out=KanbanDiagram{}; IndentLines L{src}; std::string_view line; bool header=false;
  int indent=0, col_indent=-1;
  while(L.next(line,indent)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"kanban")){header=true;continue;} continue;}
    if(line=="{"||line=="}") continue;
    if(!line.empty()&&line.front()=='@') continue;
    // Parse optional ": description" suffix after the closing bracket
    std::string_view item_part = line;
    std::string desc;
    size_t rb = line.rfind(']');
    if(rb != std::string_view::npos) {
      size_t col = line.find(':', rb+1);
      if(col != std::string_view::npos) {
        desc = std::string(tr(line.substr(col+1)));
        item_part = line.substr(0, rb+1);
      }
    }
    size_t b1=item_part.find('['),b2=item_part.find(']');
    std::string id2,lbl;
    if(b1!=std::string_view::npos&&b2!=std::string_view::npos){
      id2=std::string(tr(item_part.substr(0,b1))); lbl=std::string(tr(item_part.substr(b1+1,b2-b1-1)));
    } else {
      id2=std::string(item_part); lbl=id2;
    }
    if(id2.empty()) continue;
    // First item establishes the column indent level
    if(col_indent<0) col_indent=indent;
    if(indent<=col_indent) {
      out.columns.push_back({id2,lbl,{}});
    } else {
      if(!out.columns.empty()) out.columns.back().cards.push_back({id2,lbl,desc});
    }
  }
  return header && !out.columns.empty();
}

// ── Kanban interactive state ──────────────────────────────────────────────────
struct KanbanEditState {
  // drag
  bool drag_active = false;
  int  drag_ci     = -1;
  int  drag_ri     = -1;
  std::vector<KanbanCol> work_cols;
  int  drop_ci     = -1;
  int  drop_ri     = -1;
  // edit popup (right-click card)
  int  ctx_ci      = -1;
  int  ctx_ri      = -1;
  char edit_label[256] = {};
  char edit_desc[512]  = {};
  bool edit_focus  = false;
  // add-card popup
  int  add_ci      = -1;
  char add_label[256] = {};
  char add_desc[512]  = {};
  bool add_focus   = false;
};
static std::unordered_map<int,KanbanEditState> s_kb_states;

static std::string next_card_id(const KanbanDiagram &d)
{
  int mx = 0;
  for(auto &col : d.columns)
    for(auto &card : col.cards){
      const auto &s = card.id;
      int i = (int)s.size();
      while(i > 0 && std::isdigit((unsigned char)s[i-1])) --i;
      if(i < (int)s.size()) mx = std::max(mx, std::atoi(s.c_str()+i));
    }
  return "c" + std::to_string(mx + 1);
}

static std::string serialize_kanban(const KanbanDiagram &d)
{
  std::ostringstream s;
  s << "kanban\n";
  for(auto &col:d.columns){
    s << "  " << col.id << "[" << col.label << "]\n";
    for(auto &card:col.cards){
      s << "    " << card.id << "[" << card.label << "]";
      if(!card.description.empty()) s << ": " << card.description;
      s << "\n";
    }
  }
  return s.str();
}

static float kanban_column_width(int column_count, float available_width,
                                 float min_width, float max_width,
                                 float hgap, float pad)
{
  if(column_count <= 0) return min_width;

  const float gap_width = std::max(0, column_count - 1) * hgap;
  const float fit_width = (available_width - pad * 2.0f - gap_width) /
                          static_cast<float>(column_count);
  if(!std::isfinite(fit_width)) return min_width;
  return std::max(min_width, std::min(fit_width, max_width));
}

static size_t utf8_codepoint_length(unsigned char lead)
{
  if((lead & 0x80) == 0) return 1;
  if((lead & 0xE0) == 0xC0) return 2;
  if((lead & 0xF0) == 0xE0) return 3;
  if((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

static std::string ellipsize_to_width(const std::string &text, float max_width)
{
  if(text.empty() || ImGui::CalcTextSize(text.c_str()).x <= max_width)
    return text;

  static constexpr const char *kEllipsis = "...";
  const float ellipsis_width = ImGui::CalcTextSize(kEllipsis).x;
  if(max_width <= ellipsis_width) return kEllipsis;

  std::string out;
  out.reserve(text.size());
  for(size_t pos = 0; pos < text.size();) {
    size_t len = utf8_codepoint_length(static_cast<unsigned char>(text[pos]));
    if(pos + len > text.size()) len = 1;
    std::string next = out;
    next.append(text, pos, len);
    next += kEllipsis;
    if(ImGui::CalcTextSize(next.c_str()).x > max_width) break;
    out.append(text, pos, len);
    pos += len;
  }
  out += kEllipsis;
  return out;
}

void render_kanban(const KanbanDiagram &d, int id)
{
  ImGui::PushID(id);
  auto &es = s_kb_states[id];

  const float card_h=32.0f,col_header_h=28.0f,hgap=10.0f,vgap=6.0f,pad=10.0f;
  const float min_col_w=120.0f,max_col_w=240.0f;

  const std::vector<KanbanCol> &cols = es.drag_active ? es.work_cols : d.columns;
  int nc=(int)cols.size();
  if(nc <= 0){ ImGui::PopID(); return; }
  int max_cards=0; for(auto &c:cols) max_cards=std::max(max_cards,(int)c.cards.size());
  float col_w=kanban_column_width(nc, ImGui::GetContentRegionAvail().x,
                                  min_col_w, max_col_w, hgap, pad);
  float cw=nc*col_w+std::max(0,nc-1)*hgap+pad*2;
  // tight height: top-gap + cards + bottom-gap (one extra slot for drop indicator)
  float col_body_h=vgap+(max_cards+1)*(card_h+vgap);
  float ch=col_header_h+col_body_h+pad*2;

  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##kb", nonzero_invisible_button_size(cw, ch));
  const bool kb_hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  const bool kb_active =ImGui::IsItemActive();
  ImDrawList *dl=ImGui::GetWindowDrawList();

  const ImU32 tcol  = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill  = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord  = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hcol  = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  const ImVec2 mouse = ImGui::GetIO().MousePos;

  // ── Pre-compute card rects ────────────────────────────────────────────────
  struct CardRect { int ci,ri; float x,y; };
  std::vector<CardRect> crects;
  for(int i=0;i<nc;++i){
    float x=orig.x+pad+i*(col_w+hgap);
    float cy=orig.y+pad+col_header_h+vgap;
    for(int j=0;j<(int)cols[i].cards.size();++j){
      crects.push_back({i,j,x+4,cy});
      cy+=card_h+vgap;
    }
  }

  // ── Hit test: card under mouse ────────────────────────────────────────────
  int hov_ci=-1, hov_ri=-1;
  if(kb_hovered||es.drag_active){
    for(auto &r:crects)
      if(mouse.x>=r.x&&mouse.x<r.x+(col_w-8)&&mouse.y>=r.y&&mouse.y<r.y+card_h){
        hov_ci=r.ci; hov_ri=r.ri; break;
      }
  }

  // ── Drag start ────────────────────────────────────────────────────────────
  if(!es.drag_active && kb_active && ImGui::IsMouseDragging(0,5.0f)){
    ImVec2 dp=ImGui::GetMouseDragDelta(0);
    ImVec2 pp={mouse.x-dp.x,mouse.y-dp.y};
    for(auto &r:crects)
      if(pp.x>=r.x&&pp.x<r.x+(col_w-8)&&pp.y>=r.y&&pp.y<r.y+card_h){
        es.drag_active=true; es.drag_ci=r.ci; es.drag_ri=r.ri;
        es.work_cols=d.columns;
        es.drop_ci=r.ci; es.drop_ri=r.ri;
        break;
      }
  }

  // ── Drag update / end ─────────────────────────────────────────────────────
  if(es.drag_active){
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    for(int i=0;i<nc;++i){
      float cx=orig.x+pad+i*(col_w+hgap);
      if(mouse.x>=cx&&mouse.x<cx+col_w){
        es.drop_ci=i;
        float body_top=orig.y+pad+col_header_h+vgap;
        int nj=(int)cols[i].cards.size();
        es.drop_ri=nj;
        for(int j=0;j<nj;++j){
          if(mouse.y<body_top+j*(card_h+vgap)+card_h*0.5f){ es.drop_ri=j; break; }
        }
        break;
      }
    }
    if(!ImGui::IsMouseDown(0)){
      if(es.drop_ci>=0){
        KanbanDiagram nd=d;
        KanbanCard moved=nd.columns[es.drag_ci].cards[es.drag_ri];
        nd.columns[es.drag_ci].cards.erase(nd.columns[es.drag_ci].cards.begin()+es.drag_ri);
        int ins=es.drop_ri;
        if(es.drop_ci==es.drag_ci && es.drop_ri>es.drag_ri) --ins;
        ins=std::max(0,std::min(ins,(int)nd.columns[es.drop_ci].cards.size()));
        nd.columns[es.drop_ci].cards.insert(nd.columns[es.drop_ci].cards.begin()+ins,moved);
        g_pending_edit={id,serialize_kanban(nd)};
      }
      es.drag_active=false; es.drag_ci=-1; es.drag_ri=-1;
      es.drop_ci=-1; es.drop_ri=-1; es.work_cols.clear();
    }
  } else if(hov_ci>=0){
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  // ── Right-click ───────────────────────────────────────────────────────────
  bool open_col_ctx=false;
  if(kb_hovered && !es.drag_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
    g_consumed_right_click=true;
    if(hov_ci>=0 && hov_ri>=0){
      // Edit existing card
      es.ctx_ci=hov_ci; es.ctx_ri=hov_ri;
      const auto &card=cols[hov_ci].cards[hov_ri];
      std::strncpy(es.edit_label,card.label.c_str(),255); es.edit_label[255]='\0';
      std::strncpy(es.edit_desc, card.description.c_str(),511); es.edit_desc[511]='\0';
      es.edit_focus=true;
      ImGui::OpenPopup("##kb_edit");
    } else {
      // Right-click on empty column area → column context menu
      for(int i=0;i<nc;++i){
        float cx=orig.x+pad+i*(col_w+hgap);
        if(mouse.x>=cx&&mouse.x<cx+col_w){ es.add_ci=i; open_col_ctx=true; break; }
      }
    }
  }
  if(open_col_ctx) ImGui::OpenPopup("##kb_col_ctx");

  // ── Draw columns ─────────────────────────────────────────────────────────
  for(int i=0;i<nc;++i){
    float x=orig.x+pad+i*(col_w+hgap);
    float y=orig.y+pad;
    float body_top=y+col_header_h;
    ImU32 hc=series_color(i,0.6f);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+col_w,y+col_header_h),hc,4);
    std::string col_label=ellipsize_to_width(cols[i].label, col_w-12.0f);
    ImVec2 ls=ImGui::CalcTextSize(col_label.c_str());
    dl->AddText(ImVec2(x+(col_w-ls.x)*0.5f,y+(col_header_h-ls.y)*0.5f),tcol,col_label.c_str());
    dl->AddRectFilled(ImVec2(x,body_top),ImVec2(x+col_w,y+ch-pad*2),series_color(i,0.08f),0);
    dl->AddRect(ImVec2(x,y),ImVec2(x+col_w,y+ch-pad*2),bord,4);

    float cy=body_top+vgap;
    int nj=(int)cols[i].cards.size();
    for(int j=0;j<nj;++j){
      if(es.drag_active && es.drop_ci==i && es.drop_ri==j)
        dl->AddRectFilled(ImVec2(x+4,cy-vgap*0.5f-1),ImVec2(x+col_w-4,cy-vgap*0.5f+1),series_color(i,0.9f),2);
      bool is_dragging=(es.drag_active && i==es.drag_ci && j==es.drag_ri);
      bool is_hovered=(hov_ci==i && hov_ri==j && !es.drag_active);
      if(is_dragging){
        dl->AddRectFilled(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),
          ImGui::ColorConvertFloat4ToU32({0.5f,0.5f,0.5f,0.15f}),3);
        dl->AddRect(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),bord,3,0,1.0f);
      } else {
        dl->AddRectFilled(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),is_hovered?hcol:fill,3);
        dl->AddRect(ImVec2(x+4,cy),ImVec2(x+col_w-4,cy+card_h),is_hovered?series_color(i,0.8f):bord,3);
        const auto &card=cols[i].cards[j];
        std::string lbl=ellipsize_to_width(card.label, col_w-20.0f);
        ImVec2 cs=ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(x+4+(col_w-8-cs.x)*0.5f,cy+(card_h-cs.y)*0.5f),tcol,lbl.c_str());
        if(!card.description.empty())
          dl->AddCircleFilled(ImVec2(x+col_w-10,cy+card_h-8),3.0f,series_color(i,0.7f));
      }
      cy+=card_h+vgap;
    }
    if(es.drag_active && es.drop_ci==i && es.drop_ri>=nj)
      dl->AddRectFilled(ImVec2(x+4,cy-vgap*0.5f-1),ImVec2(x+col_w-4,cy-vgap*0.5f+1),series_color(i,0.9f),2);
  }

  // ── Floating drag card ────────────────────────────────────────────────────
  if(es.drag_active && es.drag_ci>=0 && es.drag_ci<(int)d.columns.size()
     && es.drag_ri>=0 && es.drag_ri<(int)d.columns[es.drag_ci].cards.size()){
    const auto &dc=d.columns[es.drag_ci].cards[es.drag_ri];
    float fx=mouse.x-col_w*0.5f, fy=mouse.y-card_h*0.5f;
    dl->AddRectFilled(ImVec2(fx,fy),ImVec2(fx+col_w-8,fy+card_h),
      ImGui::ColorConvertFloat4ToU32({0.2f,0.2f,0.2f,0.85f}),3);
    dl->AddRect(ImVec2(fx,fy),ImVec2(fx+col_w-8,fy+card_h),series_color(es.drag_ci,0.9f),3,0,1.5f);
    std::string lbl=ellipsize_to_width(dc.label, col_w-20.0f);
    ImVec2 cs=ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(fx+(col_w-8-cs.x)*0.5f,fy+(card_h-cs.y)*0.5f),tcol,lbl.c_str());
  }

  // ── Hover tooltip ─────────────────────────────────────────────────────────
  if(hov_ci>=0 && hov_ri>=0 && !es.drag_active){
    const auto &card=cols[hov_ci].cards[hov_ri];
    ImGui::SetTooltip("%s", card.description.empty() ? card.label.c_str() : card.description.c_str());
  }

  // ── Column context popup (right-click empty area) ─────────────────────────
  bool open_add_from_ctx = false;
  if(ImGui::BeginPopup("##kb_col_ctx")){
    if(ImGui::MenuItem("Add card")){
      es.add_label[0]='\0'; es.add_desc[0]='\0'; es.add_focus=true;
      open_add_from_ctx=true;
    }
    ImGui::EndPopup();
  }
  if(open_add_from_ctx) ImGui::OpenPopup("##kb_add");

  // ── Card edit popup (right-click card) ────────────────────────────────────
  if(ImGui::BeginPopup("##kb_edit")){
    if(es.ctx_ci>=0 && es.ctx_ci<(int)d.columns.size()
       && es.ctx_ri>=0 && es.ctx_ri<(int)d.columns[es.ctx_ci].cards.size()){
      ImGui::TextDisabled("Edit card");
      ImGui::Separator();
      ImGui::Text("Label:");
      if(es.edit_focus){ ImGui::SetKeyboardFocusHere(); es.edit_focus=false; }
      ImGui::SetNextItemWidth(220);
      ImGui::InputText("##kb_lbl",es.edit_label,256);
      ImGui::Text("Description:");
      ImGui::SetNextItemWidth(220);
      ImGui::InputTextMultiline("##kb_desc",es.edit_desc,512,ImVec2(220,60));
      ImGui::Separator();
      bool confirm = ImGui::Button("OK") ||
                     (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsItemActive());
      if(confirm && es.edit_label[0]){
        KanbanDiagram nd=d;
        nd.columns[es.ctx_ci].cards[es.ctx_ri].label       = es.edit_label;
        nd.columns[es.ctx_ci].cards[es.ctx_ri].description  = es.edit_desc;
        g_pending_edit={id,serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::SameLine();
      if(ImGui::Button("Delete")){
        KanbanDiagram nd=d;
        nd.columns[es.ctx_ci].cards.erase(nd.columns[es.ctx_ci].cards.begin()+es.ctx_ri);
        g_pending_edit={id,serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  // ── Add card popup ────────────────────────────────────────────────────────
  if(ImGui::BeginPopup("##kb_add")){
    if(es.add_ci>=0 && es.add_ci<(int)d.columns.size()){
      ImGui::TextDisabled("New card — %s", d.columns[es.add_ci].label.c_str());
      ImGui::Separator();
      ImGui::Text("Label:");
      if(es.add_focus){ ImGui::SetKeyboardFocusHere(); es.add_focus=false; }
      ImGui::SetNextItemWidth(220);
      bool enter=ImGui::InputText("##kb_add_lbl",es.add_label,256,ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Text("Description:");
      ImGui::SetNextItemWidth(220);
      ImGui::InputTextMultiline("##kb_add_desc",es.add_desc,512,ImVec2(220,60));
      ImGui::Separator();
      bool confirm=ImGui::Button("Add") || (enter && !ImGui::IsItemActive());
      if(confirm && es.add_label[0]){
        KanbanDiagram nd=d;
        KanbanCard nc_card;
        nc_card.id=next_card_id(d);
        nc_card.label=es.add_label;
        nc_card.description=es.add_desc;
        nd.columns[es.add_ci].cards.push_back(nc_card);
        g_pending_edit={id,serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ARCHITECTURE
// ═══════════════════════════════════════════════════════════════════════════
bool parse_architecture(std::string_view src, ArchDiagram &out)
{
  out=ArchDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  std::unordered_map<std::string,int> sidx,gidx;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"architecture-beta")||sw(ll,"architecture")){header=true;continue;} continue;}
    // group id(icon)[label]  or  group id(icon)[label] in parent
    if(sw(ll,"group ")){
      std::string_view rest=tr(line.substr(6));
      size_t p1=rest.find('('),p2=rest.find(')'),b1=rest.find('['),b2=rest.find(']');
      std::string gid=std::string(p1!=std::string_view::npos?tr(rest.substr(0,p1)):rest);
      std::string icon=p1!=std::string_view::npos&&p2!=std::string_view::npos?std::string(rest.substr(p1+1,p2-p1-1)):"";
      std::string lbl=b1!=std::string_view::npos&&b2!=std::string_view::npos?std::string(rest.substr(b1+1,b2-b1-1)):gid;
      int n=(int)out.groups.size(); out.groups.push_back({gid,icon,lbl}); gidx[gid]=n; continue;
    }
    // service id(icon)[label] in group
    if(sw(ll,"service ")){
      std::string_view rest=tr(line.substr(8));
      size_t p1=rest.find('('),p2=rest.find(')'),b1=rest.find('['),b2=rest.find(']');
      std::string sid=std::string(p1!=std::string_view::npos?tr(rest.substr(0,p1)):rest);
      std::string icon=p1!=std::string_view::npos&&p2!=std::string_view::npos?std::string(rest.substr(p1+1,p2-p1-1)):"";
      std::string lbl=b1!=std::string_view::npos&&b2!=std::string_view::npos?std::string(rest.substr(b1+1,b2-b1-1)):sid;
      // find "in group_id"
      std::string grp="";
      size_t in_pos=ll.find(" in "); if(in_pos!=std::string::npos) grp=std::string(tr(line.substr(in_pos+4)));
      int n=(int)out.services.size(); out.services.push_back({sid,icon,lbl,grp}); sidx[sid]=n; continue;
    }
    // edge: A:L -- R:B  or  A --> B
    std::string_view lhs,rhs; std::string lbl2;
    if(split_arrow(line,lhs,rhs,lbl2)||line.find(":L -- ")!=std::string_view::npos||line.find(":R -- ")!=std::string_view::npos){
      // strip direction qualifiers L/R/T/B
      auto strip_dir=[](std::string s)->std::string{ size_t col=s.rfind(':'); if(col!=std::string::npos){std::string dir=lc(s.substr(col+1)); if(dir=="l"||dir=="r"||dir=="t"||dir=="b") return s.substr(0,col);} return s; };
      std::string f=strip_dir(std::string(lhs)),t=strip_dir(std::string(rhs));
      if(!f.empty()&&!t.empty()) out.edges.push_back({f,t});
    }
  }
  return header && (!out.services.empty()||!out.groups.empty());
}

void render_architecture(const ArchDiagram &d, int id)
{
  ImGui::PushID(id);
  const float sw2=110.0f,sh=36.0f,hgap=40.0f,vgap=20.0f,gpad=12.0f;
  // lay out services in a grid
  int ns=(int)d.services.size(); if(ns==0) ns=1;
  int cols=std::min(4,ns), rows=(ns+cols-1)/cols;
  float cw=cols*(sw2+hgap)+hgap+gpad*2;
  float ch=rows*(sh+vgap)+vgap+gpad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##arch", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill=ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);

  // draw groups as large faint rectangles
  for(int i=0;i<(int)d.groups.size();++i){
    float gx=orig.x+gpad+i*(sw2+hgap)*0.5f, gy=orig.y+20;
    float gw=std::min(cw-gpad*2,(float)(cols*(sw2+hgap)));
    float gh=ch-40.0f;
    dl->AddRectFilled(ImVec2(gx,gy),ImVec2(gx+gw,gy+gh),series_color(i+4,0.08f),6);
    dl->AddRect(ImVec2(gx,gy),ImVec2(gx+gw,gy+gh),series_color(i+4,0.4f),6,0,1.5f);
    dl->AddText(ImVec2(gx+4,gy+2),series_color(i+4,1.0f),d.groups[i].label.c_str());
  }
  // draw services
  std::unordered_map<std::string,ImVec2> scenters;
  for(int i=0;i<(int)d.services.size();++i){
    auto [col,row]=grid_pos(i,cols);
    float x=orig.x+gpad+hgap+col*(sw2+hgap), y=orig.y+20+gpad+vgap+row*(sh+vgap);
    scenters[d.services[i].id]=ImVec2(x+sw2*0.5f,y+sh*0.5f);
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+sw2,y+sh),fill,5);
    dl->AddRect(ImVec2(x,y),ImVec2(x+sw2,y+sh),bord,5);
    // icon as text
    std::string disp=d.services[i].label;
    if(!d.services[i].icon.empty()) disp="["+d.services[i].icon+"] "+disp;
    std::string short_d=disp.size()>14?disp.substr(0,13)+"…":disp;
    ImVec2 ts=ImGui::CalcTextSize(short_d.c_str());
    dl->AddText(ImVec2(x+(sw2-ts.x)*0.5f,y+(sh-ts.y)*0.5f),tcol,short_d.c_str());
  }
  // edges
  for(auto &e:d.edges){
    auto ai=scenters.find(e.from),bi=scenters.find(e.to);
    if(ai==scenters.end()||bi==scenters.end()) continue;
    dl->AddLine(ai->second,bi->second,lcol,1.5f);
    float dx=bi->second.x-ai->second.x,dy=bi->second.y-ai->second.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;} draw_arrow_head(dl,bi->second,ImVec2(dx,dy),7,lcol);
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// RADAR CHART
// ═══════════════════════════════════════════════════════════════════════════
bool parse_radar(std::string_view src, RadarDiagram &out)
{
  out=RadarDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  RadarCurve cur_curve;
  bool in_curve=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"radar-beta")||sw(ll,"radar")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    if(sw(ll,"max ")||sw(ll,"accmax ")) { out.max_val=std::strtof(std::string(tr(line.substr(ll.find(' ')+1))).c_str(),nullptr); continue;}
    // axis line: "A, B, C, D" or axis [A, B, C]
    if(sw(ll,"axis ")){ std::string_view r=tr(line.substr(5));
      if(!r.empty()&&r.front()=='['){size_t e2=r.find(']');if(e2!=std::string_view::npos)r=r.substr(1,e2-1);}
      std::string r_s(r); std::istringstream ss2(r_s); std::string tok;
      while(std::getline(ss2,tok,',')) out.axes.push_back(strip_quotes(tr(tok)));
      continue;
    }
    // curve block: "Name {" then "data [v1,v2,...]" then "}"
    if(line=="}"){ if(in_curve){out.curves.push_back(cur_curve);} in_curve=false; cur_curve=RadarCurve{}; continue;}
    if(line.back()=='{'){
      in_curve=true; cur_curve.name=std::string(tr(line.substr(0,line.size()-1))); continue;
    }
    if(in_curve&&sw(ll,"data [")){
      size_t b=line.find('['),e2=line.find(']');
      if(b!=std::string_view::npos&&e2!=std::string_view::npos){
        std::string inner=std::string(line.substr(b+1,e2-b-1));
        std::istringstream ss2(inner); std::string tok;
        while(std::getline(ss2,tok,',')) cur_curve.values.push_back(std::strtof(std::string(tr(tok)).c_str(),nullptr));
      }
      continue;
    }
    // simple "Label: value" format (alternative syntax)
    size_t col=line.find(':');
    if(col!=std::string_view::npos&&!in_curve){
      std::string axis_name=strip_quotes(line.substr(0,col));
      float val=std::strtof(std::string(tr(line.substr(col+1))).c_str(),nullptr);
      out.axes.push_back(axis_name);
      if(out.curves.empty()) out.curves.push_back({"Values",{}});
      out.curves[0].values.push_back(val);
      out.max_val=std::max(out.max_val,val);
    }
  }
  if(in_curve) out.curves.push_back(cur_curve);
  return header && (!out.axes.empty()||!out.curves.empty());
}

void render_radar(const RadarDiagram &d, int id)
{
  ImGui::PushID(id);
  const float r=110.0f,pad=60.0f;
  float cw=r*2+pad*2, ch=r*2+pad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##radar", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol=ImGui::GetColorU32(ImGuiCol_Separator);
  ImVec2 center(orig.x+pad+r,orig.y+20+pad+r);
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y+2),tcol,d.title.c_str());}
  int na=(int)d.axes.size(); if(na<3){ImGui::Text("Need >= 3 axes");ImGui::PopID();return;}
  // web lines
  for(int level=1;level<=4;++level){
    float lr=r*level*0.25f;
    std::vector<ImVec2> pts;
    for(int a=0;a<=na;++a){
      float angle=-kPi*0.5f+a*(2*kPi/na);
      pts.push_back(ImVec2(center.x+std::cos(angle)*lr,center.y+std::sin(angle)*lr));
    }
    for(int a=0;a<na;++a) dl->AddLine(pts[a],pts[a+1],gcol,1.0f);
  }
  // spokes
  for(int a=0;a<na;++a){
    float angle=-kPi*0.5f+a*(2*kPi/na);
    ImVec2 tip(center.x+std::cos(angle)*r,center.y+std::sin(angle)*r);
    dl->AddLine(center,tip,gcol,1.0f);
    // axis label
    const std::string &lbl=d.axes[a];
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    float lx=center.x+std::cos(angle)*(r+14)-ts.x*0.5f;
    float ly=center.y+std::sin(angle)*(r+14)-ts.y*0.5f;
    dl->AddText(ImVec2(lx,ly),tcol,lbl.c_str());
  }
  // curves
  float max_v=d.max_val>0?d.max_val:100.0f;
  for(int ci=0;ci<(int)d.curves.size();++ci){
    auto &c=d.curves[ci];
    if(c.values.empty()) continue;
    std::vector<ImVec2> pts;
    for(int a=0;a<na;++a){
      float v=a<(int)c.values.size()?c.values[a]:0.0f;
      float frac=v/max_v; float angle=-kPi*0.5f+a*(2*kPi/na);
      pts.push_back(ImVec2(center.x+std::cos(angle)*r*frac,center.y+std::sin(angle)*r*frac));
    }
    ImU32 cc=series_color(ci,0.35f);
    ImU32 cc2=series_color(ci,0.85f);
    dl->AddConvexPolyFilled(pts.data(),(int)pts.size(),cc);
    pts.push_back(pts[0]);
    for(int k=0;k<(int)pts.size()-1;++k) dl->AddLine(pts[k],pts[k+1],cc2,2.0f);
    if(!c.name.empty()){ ImVec2 ts=ImGui::CalcTextSize(c.name.c_str()); dl->AddText(ImVec2(orig.x+pad+(float)ci*60.0f,orig.y+cw-20),cc2,c.name.c_str()); }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// TREEMAP
// ═══════════════════════════════════════════════════════════════════════════
bool parse_treemap(std::string_view src, TreemapDiagram &out)
{
  out=TreemapDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  // track indent to build tree
  std::vector<int> level_stack; level_stack.push_back(-1);
  std::vector<int> parent_at_level(20,-1);
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"treemap-beta")||sw(ll,"treemap")){header=true;continue;} continue;}
    if(sw(ll,"title ")) continue;
    int indent=0; for(char c:line){if(c==' ')indent++;else if(c=='\t')indent+=2;else break;}
    int level=indent/2;
    std::string_view l=tr(line);
    if(l.empty()) continue;
    size_t col=l.find(':');
    std::string name=col!=std::string_view::npos?strip_quotes(l.substr(0,col)):std::string(l);
    float val=col!=std::string_view::npos?std::strtof(std::string(tr(l.substr(col+1))).c_str(),nullptr):0.0f;
    int par=level>0?parent_at_level[level-1]:-1;
    TreemapNode node; node.name=name; node.value=val; node.parent=par;
    int ni=(int)out.nodes.size();
    if(par>=0) out.nodes[par].children.push_back(ni);
    parent_at_level[level]=ni;
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}

void render_treemap(const TreemapDiagram &d, int id)
{
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float cw=340.0f, ch=200.0f;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tm", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);

  // compute total value for root children
  std::function<float(int)> total_val=[&](int ni)->float{
    if(!d.nodes[ni].children.empty()){
      float s=0; for(int c:d.nodes[ni].children) s+=total_val(c); return s;
    }
    return d.nodes[ni].value>0?d.nodes[ni].value:1.0f;
  };

  // squarified treemap recursive layout
  std::function<void(int,ImVec2,ImVec2,int)> layout=[&](int ni,ImVec2 tl,ImVec2 br,int depth){
    auto &node=d.nodes[ni];
    ImU32 fc=series_color(ni+depth*3,0.6f-depth*0.1f);
    dl->AddRectFilled(tl,br,fc,2);
    dl->AddRect(tl,br,bord,2,0,1.5f);
    float fw=br.x-tl.x, fh=br.y-tl.y;
    std::string lbl=node.name.size()>(size_t)(fw/7)?node.name.substr(0,(size_t)fw/7)+"…":node.name;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    if(ts.x<=fw-4&&ts.y<=fh-4) dl->AddText(ImVec2(tl.x+(fw-ts.x)*0.5f,tl.y+(fh-ts.y)*0.5f),tcol,lbl.c_str());
    if(node.children.empty()) return;
    float tot=total_val(ni); if(tot<=0) return;
    // horizontal split
    float x=tl.x;
    for(int ci:node.children){
      float frac=total_val(ci)/tot;
      float nx=x+fw*frac;
      if(depth%2==0) layout(ci,ImVec2(x,tl.y+16),ImVec2(nx,br.y),depth+1);
      else {
        float y=tl.y+16+(br.y-tl.y-16)*0;
        layout(ci,ImVec2(tl.x,y),ImVec2(br.x,tl.y+16+(br.y-tl.y-16)*frac),depth+1);
      }
      x=nx;
    }
  };

  // find roots (parent==-1)
  std::vector<int> roots;
  for(int i=0;i<(int)d.nodes.size();++i) if(d.nodes[i].parent<0) roots.push_back(i);
  if(roots.empty()){ ImGui::Text("(empty treemap)"); ImGui::PopID(); return; }
  if(roots.size()==1){ layout(roots[0],orig,ImVec2(orig.x+cw,orig.y+ch),0); }
  else {
    float tot=0; for(int r:roots) tot+=total_val(r); if(tot<=0) tot=1;
    float x=orig.x;
    for(int r:roots){ float fw2=cw*total_val(r)/tot; layout(r,ImVec2(x,orig.y),ImVec2(x+fw2,orig.y+ch),0); x+=fw2; }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ZENUML  (rendered as sequence diagram)
// ═══════════════════════════════════════════════════════════════════════════
bool parse_zenuml(std::string_view src, SequenceDiagram &out)
{
  // ZenUML uses "A.method(B)" syntax — convert to sequence diagram events
  out=SequenceDiagram{};
  std::unordered_map<std::string,int> pidx;
  auto ensure_part=[&](const std::string &id){
    auto it=pidx.find(id); if(it!=pidx.end()) return it->second;
    SeqParticipant p; p.id=id; p.label=id;
    int n=(int)out.participants.size(); out.participants.push_back(p); pidx[id]=n; return n;
  };
  Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"zenuml")){header=true;continue;} continue;}
    if(sw(ll,"title ")){ out.title=std::string(tr(line.substr(6)));continue;}
    // participant/actor declarations
    if(sw(ll,"@")){ std::string_view rest=tr(line.substr(1)); std::string name=std::string(rest.substr(0,rest.find(' '))); ensure_part(name); continue; }
    // A.method(B) → message from A to B
    size_t dot=line.find('.'); size_t p1=line.find('('); size_t p2=line.find(')');
    if(dot!=std::string_view::npos&&p1!=std::string_view::npos&&p1>dot){
      std::string from=std::string(tr(line.substr(0,dot)));
      std::string method=std::string(tr(line.substr(dot+1,p1-dot-1)));
      std::string to=p2!=std::string_view::npos?strip_quotes(line.substr(p1+1,p2-p1-1)):from;
      if(to.empty()) to=from;
      ensure_part(from); ensure_part(to);
      SeqMessage msg{from,to,method,false,true};
      int mi=(int)out.messages.size(); out.messages.push_back(msg);
      out.events.push_back({SequenceDiagram::Event::T::Message,mi,"","",""});
    }
  }
  return header && !out.participants.empty();
}


// ═══════════════════════════════════════════════════════════════════════════
// EVENT MODELING
// ═══════════════════════════════════════════════════════════════════════════
bool parse_eventmodeling(std::string_view src, EventModelingDiagram &out)
{
  out=EventModelingDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"eventmodeling")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    // command/event/readmodel/policy/processor keywords
    if(sw(ll,"command "))   { out.items.push_back({EMItem::T::Command,   strip_quotes(line.substr(8))});  continue;}
    if(sw(ll,"event "))     { out.items.push_back({EMItem::T::Event,     strip_quotes(line.substr(6))});  continue;}
    if(sw(ll,"readmodel ")  ||sw(ll,"read_model ")) { out.items.push_back({EMItem::T::ReadModel,strip_quotes(line.substr(ll.find(' ')+1))}); continue;}
    if(sw(ll,"policy "))    { out.items.push_back({EMItem::T::Policy,    strip_quotes(line.substr(7))});  continue;}
    if(sw(ll,"processor ")) { out.items.push_back({EMItem::T::Processor, strip_quotes(line.substr(10))}); continue;}
    // arrow: A --> B
    std::string_view lhs,rhs; std::string lbl;
    if(split_arrow(line,lhs,rhs,lbl)) out.links.push_back({std::string(lhs),std::string(rhs)});
  }
  return header && !out.items.empty();
}

void render_eventmodeling(const EventModelingDiagram &d, int id)
{
  ImGui::PushID(id);
  const float iw=120.0f,ih=40.0f,hgap=14.0f,pad=12.0f;
  int n=(int)d.items.size(); if(n==0){ImGui::Text("(empty event model)");ImGui::PopID();return;}
  float cw=n*(iw+hgap)+hgap+pad*2, ch=ih+pad*2+20;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##em", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}
  // type → color hue
  static const float hues[]={0.6f,0.1f,0.35f,0.75f,0.5f}; // command=blue,event=orange,readmodel=green,policy=purple,processor=teal
  std::unordered_map<std::string,int> item_idx;
  for(int i=0;i<n;++i){
    float x=orig.x+pad+i*(iw+hgap), y=orig.y+20+pad;
    int ti=(int)d.items[i].type;
    float rr,gg,bb; ImGui::ColorConvertHSVtoRGB(hues[ti],0.6f,0.88f,rr,gg,bb);
    ImU32 fc=ImGui::GetColorU32(ImVec4(rr,gg,bb,0.75f));
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+iw,y+ih),fc,5);
    dl->AddRect(ImVec2(x,y),ImVec2(x+iw,y+ih),bord,5);
    std::string lbl=d.items[i].name.size()>14?d.items[i].name.substr(0,13)+"…":d.items[i].name;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(x+(iw-ts.x)*0.5f,y+(ih-ts.y)*0.5f),tcol,lbl.c_str());
    item_idx[d.items[i].name]=i;
    // draw arrows to next item in sequence
    if(i<n-1){
      ImVec2 a(x+iw,y+ih*0.5f), b(x+iw+hgap,y+ih*0.5f);
      dl->AddLine(a,b,ImGui::GetColorU32(ImGuiCol_TextDisabled),1.5f);
      draw_arrow_head(dl,b,ImVec2(1,0),7,ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// VENN DIAGRAM
// ═══════════════════════════════════════════════════════════════════════════
bool parse_venn(std::string_view src, VennDiagram &out)
{
  out=VennDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"venn")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    // set: id "Label"  or  id  "Label"
    // intersection: A&B "label"  or  A+B "label"
    size_t amp=line.find('&'), plus2=line.find('+');
    size_t sep=(amp!=std::string_view::npos)?amp:(plus2!=std::string_view::npos?plus2:std::string_view::npos);
    if(sep!=std::string_view::npos){
      std::string ids=std::string(tr(line));
      // find label in quotes
      size_t q1=ids.find('"'),q2=ids.rfind('"');
      std::string lbl=q1!=std::string::npos&&q2>q1?ids.substr(q1+1,q2-q1-1):"";
      // split ids by & or +
      std::string id_part=ids.substr(0,q1!=std::string::npos?q1:ids.size());
      VennIntersection vi; vi.label=lbl;
      std::istringstream ss2(id_part); std::string tok;
      while(std::getline(ss2,tok,amp!=std::string::npos?'&':'+')) vi.set_ids.push_back(std::string(tr(tok)));
      out.intersections.push_back(vi);
    } else {
      // bare set: id ["label"]
      std::string ls=std::string(line);
      size_t q1=ls.find('"'),q2=ls.rfind('"');
      std::string set_id=q1!=std::string::npos?std::string(tr(ls.substr(0,q1))):ls;
      std::string lbl2=q1!=std::string::npos&&q2>q1?ls.substr(q1+1,q2-q1-1):set_id;
      set_id=std::string(tr(set_id));
      if(!set_id.empty()) out.sets.push_back({set_id,lbl2});
    }
  }
  return header && !out.sets.empty();
}

void render_venn(const VennDiagram &d, int id)
{
  ImGui::PushID(id);
  int ns=(int)d.sets.size(); if(ns<2){ ImGui::Text("Need >= 2 sets for Venn"); ImGui::PopID(); return; }
  const float r=58.0f, pad=20.0f, title_h=20.0f;

  // Compute canvas size
  float cw, ch;
  if(ns==3){
    cw = r*3.8f + pad*2;
    ch = r*3.2f + pad*2 + title_h;
  } else {
    float overlap=r*0.38f;
    cw = ns*r*2-(ns-1)*overlap + pad*2;
    ch = r*2 + pad*2 + title_h + 20.0f;
  }

  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##venn", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);

  // Compute circle centers relative to orig
  std::vector<ImVec2> set_centers(ns);
  if(ns==3){
    // Triangular layout: A top-center, B bottom-left, C bottom-right
    float cx=orig.x+cw*0.5f, cy=orig.y+title_h+pad+r*1.15f;
    float ox=r*0.78f, oy=r*0.45f;
    set_centers[0]=ImVec2(cx,    cy-oy);   // A: top
    set_centers[1]=ImVec2(cx-ox, cy+oy);   // B: bottom-left
    set_centers[2]=ImVec2(cx+ox, cy+oy);   // C: bottom-right
  } else {
    float overlap=r*0.38f;
    float start_x=orig.x+pad+r;
    float cy=orig.y+title_h+pad+r;
    for(int i=0;i<ns;++i) set_centers[i]=ImVec2(start_x+i*(r*2-overlap),cy);
  }

  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}

  // Draw circles
  for(int i=0;i<ns;++i){
    float rr,gg,bb; ImGui::ColorConvertHSVtoRGB((float)i/ns,0.55f,0.9f,rr,gg,bb);
    dl->AddCircleFilled(set_centers[i],r,ImGui::GetColorU32(ImVec4(rr,gg,bb,0.22f)));
    dl->AddCircle(set_centers[i],r,ImGui::GetColorU32(ImVec4(rr,gg,bb,0.8f)),0,2.0f);
  }

  // Set labels — placed outside each circle's overlap direction
  for(int i=0;i<ns;++i){
    std::string lbl=d.sets[i].label;
    ImVec2 ts=ImGui::CalcTextSize(lbl.c_str());
    ImVec2 lp;
    if(ns==3){
      if(i==0) lp=ImVec2(set_centers[0].x-ts.x*0.5f, set_centers[0].y-r-ts.y-2); // above A
      else if(i==1) lp=ImVec2(set_centers[1].x-ts.x-r*0.15f, set_centers[1].y+r*0.55f);  // below-left of B
      else          lp=ImVec2(set_centers[2].x+r*0.15f,       set_centers[2].y+r*0.55f);  // below-right of C
    } else {
      lp=ImVec2(set_centers[i].x-ts.x*0.5f, set_centers[i].y+r+4);
    }
    dl->AddText(lp, tcol, lbl.c_str());
  }

  // Intersection labels at the centroid of the referenced set centers
  for(auto &vi:d.intersections){
    if(vi.label.empty()||vi.set_ids.size()<2) continue;
    float ix=0,iy=0; int cnt=0;
    for(auto &sid:vi.set_ids){
      for(int i=0;i<ns;++i){ if(d.sets[i].id==sid){ix+=set_centers[i].x;iy+=set_centers[i].y;cnt++;break;} }
    }
    if(cnt>0){ ix/=cnt; iy/=cnt; ImVec2 ts=ImGui::CalcTextSize(vi.label.c_str()); dl->AddText(ImVec2(ix-ts.x*0.5f,iy-ts.y*0.5f),tcol,vi.label.c_str()); }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// ISHIKAWA (FISHBONE)
// ═══════════════════════════════════════════════════════════════════════════
bool parse_ishikawa(std::string_view src, IshikawaDiagram &out)
{
  out=IshikawaDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"ishikawa")){header=true;continue;} continue;}
    if(sw(ll,"effect ")){ out.effect=strip_quotes(line.substr(7));continue;}
    if(sw(ll,"category ")){ out.categories.push_back({strip_quotes(line.substr(9)),{}});continue;}
    // cause: bare text indented
    int indent=0; for(char c:line){if(c==' ')indent++;else if(c=='\t')indent+=2;else break;}
    std::string_view l=tr(line);
    if(!l.empty()&&!sw(lc(l),"cause ")&&!sw(lc(l),"sub ")&&!out.categories.empty()){
      IshikawaCause c; c.text=std::string(l);
      out.categories.back().causes.push_back(c);
    }
    if(sw(lc(l),"cause ")||sw(lc(l),"sub ")){
      size_t sp=l.find(' ');
      if(!out.categories.empty()){ IshikawaCause c; c.text=strip_quotes(l.substr(sp)); out.categories.back().causes.push_back(c); }
    }
  }
  return header && !out.effect.empty();
}

void render_ishikawa(const IshikawaDiagram &d, int id)
{
  ImGui::PushID(id);
  const float cw=420.0f,ch=200.0f,effect_w=80.0f;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ish", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bord=ImGui::GetColorU32(ImGuiCol_Border);
  // spine
  float sy=orig.y+ch*0.5f;
  dl->AddLine(ImVec2(orig.x+20,sy),ImVec2(orig.x+cw-effect_w-10,sy),tcol,2.5f);
  // effect box
  float ex=orig.x+cw-effect_w;
  dl->AddRectFilled(ImVec2(ex,sy-18),ImVec2(ex+effect_w,sy+18),ImGui::GetColorU32(ImVec4(0.8f,0.3f,0.3f,0.7f)),4);
  std::string eff=d.effect.size()>10?d.effect.substr(0,9)+"…":d.effect;
  ImVec2 es=ImGui::CalcTextSize(eff.c_str()); dl->AddText(ImVec2(ex+(effect_w-es.x)*0.5f,sy-es.y*0.5f),tcol,eff.c_str());
  // categories as bones
  int nc=(int)d.categories.size(); if(nc==0){ImGui::PopID();return;}
  float bone_spacing=(cw-effect_w-40.0f)/std::max(1,nc);
  for(int i=0;i<nc;++i){
    float bx=orig.x+20.0f+i*bone_spacing+bone_spacing*0.5f;
    bool top=(i%2==0);
    float ey2=top?orig.y+20:orig.y+ch-20;
    // bone
    dl->AddLine(ImVec2(bx,ey2),ImVec2(bx+(top?20:-20),sy),lcol,1.5f);
    // category label
    const std::string &cat=d.categories[i].name;
    ImVec2 cs=ImGui::CalcTextSize(cat.c_str());
    dl->AddText(ImVec2(bx-cs.x*0.5f,top?ey2-cs.y-2:ey2+2),tcol,cat.c_str());
    // causes as sub-bones
    for(int j=0;j<(int)d.categories[i].causes.size()&&j<4;++j){
      float cx2=bx+(j+1)*-12.0f*(top?-1:1);
      float cy2=top?sy-(sy-ey2)*((j+1)*0.25f):sy+(ey2-sy)*((j+1)*0.25f);
      dl->AddLine(ImVec2(cx2,cy2-8),ImVec2(cx2,cy2+8),lcol,1.0f);
      const std::string &cause=d.categories[i].causes[j].text;
      std::string sc=cause.size()>10?cause.substr(0,9)+"…":cause;
      ImVec2 ls=ImGui::CalcTextSize(sc.c_str());
      dl->AddText(ImVec2(cx2-ls.x*0.5f,top?cy2-ls.y-10:cy2+10),lcol,sc.c_str());
    }
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// WARDLEY MAP
// ═══════════════════════════════════════════════════════════════════════════
bool parse_wardley(std::string_view src, WardleyDiagram &out)
{
  out=WardleyDiagram{}; Lines L{src}; std::string_view line; bool header=false;
  while(L.next(line)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"wardley")){header=true;continue;} continue;}
    if(sw(ll,"title ")) { out.title=strip_quotes(line.substr(6));continue;}
    // component name [visibility, evolution]
    if(sw(ll,"component ")||sw(ll,"note ")){
      std::string_view rest=tr(line.substr(sw(ll,"component ")?10:5));
      size_t b1=rest.find('['),b2=rest.find(']');
      std::string name=std::string(b1!=std::string_view::npos?tr(rest.substr(0,b1)):rest);
      float vis=0.5f,evo=0.5f;
      if(b1!=std::string_view::npos&&b2!=std::string_view::npos){
        std::string coords=std::string(rest.substr(b1+1,b2-b1-1));
        size_t comma=coords.find(',');
        if(comma!=std::string::npos){ vis=std::strtof(coords.substr(0,comma).c_str(),nullptr); evo=std::strtof(coords.substr(comma+1).c_str(),nullptr); }
      }
      out.components.push_back({name,vis,evo}); continue;
    }
    // link: A -> B
    std::string_view lhs,rhs; std::string lbl;
    if(split_arrow(line,lhs,rhs,lbl)) out.links.push_back({std::string(lhs),std::string(rhs)});
  }
  return header && !out.components.empty();
}

void render_wardley(const WardleyDiagram &d, int id)
{
  ImGui::PushID(id);
  const float cw=320.0f,ch=240.0f,pad=40.0f;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##wd", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol=ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);
  float pw=cw-pad*2, ph=ch-pad*2-20;
  ImVec2 tl(orig.x+pad,orig.y+20+pad);
  // axes
  dl->AddLine(ImVec2(tl.x,tl.y),ImVec2(tl.x,tl.y+ph),tcol,1.5f);
  dl->AddLine(ImVec2(tl.x,tl.y+ph),ImVec2(tl.x+pw,tl.y+ph),tcol,1.5f);
  // axis labels
  dl->AddText(ImVec2(tl.x-2,tl.y-14),lcol,"Visible");
  dl->AddText(ImVec2(tl.x-2,tl.y+ph+2),lcol,"Invisible");
  const char *stages[]={"Genesis","Custom","Product","Commodity"};
  for(int s=0;s<4;++s){ImVec2 ts=ImGui::CalcTextSize(stages[s]);dl->AddText(ImVec2(tl.x+pw*s/3.0f-ts.x*0.5f,tl.y+ph+14),lcol,stages[s]);}
  // grid
  for(int s=1;s<4;++s){float gx=tl.x+pw*s/4.0f;dl->AddLine(ImVec2(gx,tl.y),ImVec2(gx,tl.y+ph),gcol,1.0f);}
  if(!d.title.empty()){ImVec2 ts=ImGui::CalcTextSize(d.title.c_str());dl->AddText(ImVec2(orig.x+(cw-ts.x)*0.5f,orig.y),tcol,d.title.c_str());}
  // components
  std::unordered_map<std::string,ImVec2> comp_pos;
  for(int i=0;i<(int)d.components.size();++i){
    auto &c=d.components[i];
    float px=tl.x+c.evolution*pw, py=tl.y+(1.0f-c.visibility)*ph;
    comp_pos[c.name]=ImVec2(px,py);
    ImU32 cc=series_color(i);
    dl->AddCircleFilled(ImVec2(px,py),6,cc);
    ImVec2 ts=ImGui::CalcTextSize(c.name.c_str());
    dl->AddText(ImVec2(px-ts.x*0.5f,py-ts.y-4),tcol,c.name.c_str());
  }
  // links
  for(auto &l:d.links){
    auto ai=comp_pos.find(l.from),bi=comp_pos.find(l.to);
    if(ai==comp_pos.end()||bi==comp_pos.end()) continue;
    dl->AddLine(ai->second,bi->second,lcol,1.5f);
    float dx=bi->second.x-ai->second.x,dy=bi->second.y-ai->second.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>1){dx/=len;dy/=len;} draw_arrow_head(dl,bi->second,ImVec2(dx,dy),7,lcol);
  }
  ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════════════════════
// TREEVIEW
// ═══════════════════════════════════════════════════════════════════════════
bool parse_treeview(std::string_view src, TreeViewDiagram &out)
{
  out=TreeViewDiagram{}; bool header=false;
  std::vector<int> parent_at_level(20,-1);
  IndentLines L{src}; std::string_view line; int indent=0;
  while(L.next(line,indent)){
    std::string ll=lc(line);
    if(!header){if(sw(ll,"treeview")){header=true;continue;} continue;}
    int level=indent/2;
    std::string_view l=tr(line); if(l.empty()) continue;
    // strip tree drawing chars: ├─, └─, │, etc.
    while(!l.empty()&&(l[0]==static_cast<char>(0xE2)||l[0]=='|'||l[0]=='-'||l[0]=='+'||l[0]==' '||l[0]=='`'||l[0]=='\\')){
      if(l.size()>=3&&(unsigned char)l[0]==0xE2) l=l.substr(3); else l=l.substr(1);
    }
    l=tr(l); if(l.empty()) continue;
    std::string lbl=strip_quotes(l);
    int par=level>0?parent_at_level[level-1]:-1;
    TVNode node; node.label=lbl; node.parent=par;
    int ni=(int)out.nodes.size();
    if(par>=0) out.nodes[par].children.push_back(ni);
    parent_at_level[std::min(level,19)]=ni;
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}

void render_treeview(const TreeViewDiagram &d, int id)
{
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float row_h=22.0f,indent_w=18.0f,pad=8.0f;
  int n=(int)d.nodes.size();
  float max_depth=0; for(auto &nd:d.nodes){ int dep=0; int p=nd.parent; while(p>=0){dep++;p=d.nodes[p].parent;} max_depth=std::max(max_depth,(float)dep); }
  float cw=pad+max_depth*indent_w+200.0f, ch=n*row_h+pad*2;
  const ImVec2 orig=ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tv", nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl=ImGui::GetWindowDrawList();
  const ImU32 tcol=ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol=ImGui::GetColorU32(ImGuiCol_TextDisabled);

  // DFS traversal to compute depths and draw
  std::function<void(int,int,int&)> draw_node=[&](int ni,int depth,int &row){
    float x=orig.x+pad+depth*indent_w;
    float y=orig.y+pad+row*row_h;
    // connector lines
    if(depth>0){
      dl->AddLine(ImVec2(x-indent_w+6,y+row_h*0.5f),ImVec2(x,y+row_h*0.5f),lcol,1.0f);
      dl->AddLine(ImVec2(x-indent_w+6,y-row_h*0.5f),ImVec2(x-indent_w+6,y+row_h*0.5f),lcol,1.0f);
    }
    // bullet
    bool has_children=!d.nodes[ni].children.empty();
    if(has_children) dl->AddTriangleFilled(ImVec2(x,y+row_h*0.5f-4),ImVec2(x,y+row_h*0.5f+4),ImVec2(x+6,y+row_h*0.5f),series_color(depth));
    else dl->AddCircleFilled(ImVec2(x+3,y+row_h*0.5f),3,series_color(depth,0.7f));
    dl->AddText(ImVec2(x+10,y+(row_h-ImGui::GetTextLineHeight())*0.5f),tcol,d.nodes[ni].label.c_str());
    row++;
    for(int c:d.nodes[ni].children) draw_node(c,depth+1,row);
  };
  int row=0;
  for(int i=0;i<n;++i) if(d.nodes[i].parent<0) draw_node(i,0,row);
  ImGui::PopID();
}

} // namespace MermaidDiagrams

