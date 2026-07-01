#include "mermaid.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <cmath>
#include <imgui.h>

namespace MermaidFlowchart
{
namespace
{
static ImVec2 nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static std::string to_lower(std::string_view s)
{
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

static bool is_ident_char(char c)
{
  const unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) != 0 || c == '_' || c == '-';
}

static bool parse_header(std::string_view line, Direction &dir)
{
  line = StringUtils::trim(line);
  if(line.empty()) return false;

  size_t sp = line.find_first_of(" \t");
  std::string_view kw = (sp == std::string_view::npos) ? line : line.substr(0, sp);
  std::string lkw = to_lower(kw);
  if(lkw != "flowchart" && lkw != "graph") return false;

  if(sp == std::string_view::npos)
  {
    dir = Direction::TB;
    return true;
  }

  std::string d = to_lower(StringUtils::trim(line.substr(sp + 1)));
  if(d == "tb" || d == "td")
    dir = Direction::TB;
  else if(d == "bt")
    dir = Direction::BT;
  else if(d == "lr")
    dir = Direction::LR;
  else if(d == "rl")
    dir = Direction::RL;
  else
    dir = Direction::TB;
  return true;
}

static bool split_edge(std::string_view line, std::string_view &lhs, std::string_view &rhs)
{
  static const char *kOps[] = {"<-->", "-->", "<--", "-.->", "---", "==>"};
  size_t best = std::string_view::npos;
  size_t best_len = 0;
  for(const char *op : kOps)
  {
    size_t p = line.find(op);
    if(p != std::string_view::npos && (best == std::string_view::npos || p < best))
    {
      best = p;
      best_len = std::char_traits<char>::length(op);
    }
  }
  if(best == std::string_view::npos) return false;

  lhs = StringUtils::trim(line.substr(0, best));
  rhs = StringUtils::trim(line.substr(best + best_len));

  if(StringUtils::starts_with(rhs, "|"))
  {
    size_t e = rhs.find('|', 1);
    if(e != std::string_view::npos)
      rhs = StringUtils::trim(rhs.substr(e + 1));
  }
  return !lhs.empty() && !rhs.empty();
}

static bool parse_node_ref(std::string_view s, std::string &id, std::string &label)
{
  s = StringUtils::trim(s);
  if(s.empty()) return false;

  size_t i = 0;
  while(i < s.size() && is_ident_char(s[i])) ++i;
  if(i == 0) return false;

  id.assign(s.substr(0, i));
  label = id;

  if(i < s.size())
  {
    const char open = s[i];
    char close = 0;
    if(open == '[') close = ']';
    if(open == '(') close = ')';
    if(open == '{') close = '}';
    if(close != 0)
    {
      size_t j = s.rfind(close);
      if(j != std::string_view::npos && j > i + 1)
      {
        std::string_view inner = StringUtils::trim(s.substr(i + 1, j - i - 1));
        if(!inner.empty()) label.assign(inner);
      }
    }
  }

  if(label.size() >= 2 && label.front() == '"' && label.back() == '"')
    label = label.substr(1, label.size() - 2);
  return true;
}

static int get_node(Graph &g, std::unordered_map<std::string, int> &idx, const std::string &id, const std::string &label)
{
  auto it = idx.find(id);
  if(it != idx.end())
  {
    if(!label.empty()) g.nodes[(size_t)it->second].label = label;
    return it->second;
  }
  const int n = (int)g.nodes.size();
  g.nodes.push_back({id, label});
  idx.emplace(id, n);
  return n;
}
} // namespace

bool parse(std::string_view src, Graph &out)
{
  out = Graph{};
  std::unordered_map<std::string, int> idx;
  bool header_seen = false;

  size_t p = 0;
  while(p < src.size())
  {
    size_t e = src.find('\n', p);
    if(e == std::string_view::npos) e = src.size();
    std::string_view line = StringUtils::trim(src.substr(p, e - p));
    p = (e < src.size()) ? e + 1 : e;

    if(line.empty() || StringUtils::starts_with(line, "%%")) continue;

    if(!header_seen)
    {
      if(!parse_header(line, out.direction)) return false;
      header_seen = true;
      continue;
    }

    if(StringUtils::starts_with(line, "subgraph") || line == "end") continue;

    std::string_view lhs, rhs;
    if(!split_edge(line, lhs, rhs)) continue;

    std::string a_id, a_label, b_id, b_label;
    if(!parse_node_ref(lhs, a_id, a_label)) continue;
    if(!parse_node_ref(rhs, b_id, b_label)) continue;

    int a = get_node(out, idx, a_id, a_label);
    int b = get_node(out, idx, b_id, b_label);
    if(a >= 0 && b >= 0) out.edges.push_back({a, b});
  }

  return header_seen && !out.nodes.empty();
}

void render(const Graph &g, int id)
{
  if(g.nodes.empty()) return;

  std::vector<int> indeg(g.nodes.size(), 0);
  for(const auto &e : g.edges)
  {
    if(e.to >= 0 && e.to < (int)indeg.size()) indeg[(size_t)e.to]++;
  }

  std::vector<int> level(g.nodes.size(), 0);
  for(size_t it = 0; it < g.nodes.size(); ++it)
  {
    for(const auto &e : g.edges)
    {
      if(e.from < 0 || e.to < 0) continue;
      level[(size_t)e.to] = std::max(level[(size_t)e.to], level[(size_t)e.from] + 1);
    }
  }

  int max_level = 0;
  for(int l : level) max_level = std::max(max_level, l);
  if(g.direction == Direction::BT || g.direction == Direction::RL)
  {
    for(int &l : level) l = max_level - l;
  }

  std::vector<std::vector<int>> by_level((size_t)max_level + 1);
  for(size_t i = 0; i < g.nodes.size(); ++i) by_level[(size_t)level[i]].push_back((int)i);

  float node_w = 120.0f;
  float node_h = 32.0f;
  for(const auto &n : g.nodes)
  {
    ImVec2 sz = ImGui::CalcTextSize(n.label.c_str());
    node_w = std::max(node_w, std::floor(sz.x + 20.0f));
    node_h = std::max(node_h, std::floor(sz.y + 12.0f));
  }

  const float sx = 48.0f;
  const float sy = 24.0f;
  float canvas_w = 0.0f, canvas_h = 0.0f;
  if(g.direction == Direction::LR || g.direction == Direction::RL)
  {
    canvas_w = (max_level + 1) * node_w + max_level * sx + 8.0f;
    int rows = 0;
    for(const auto &lv : by_level) rows = std::max(rows, (int)lv.size());
    canvas_h = rows * node_h + std::max(0, rows - 1) * sy + 8.0f;
  }
  else
  {
    canvas_h = (max_level + 1) * node_h + max_level * sy + 8.0f;
    int cols = 0;
    for(const auto &lv : by_level) cols = std::max(cols, (int)lv.size());
    canvas_w = cols * node_w + std::max(0, cols - 1) * sx + 8.0f;
  }

  ImGui::PushID(id);
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##flowchart_canvas", nonzero_invisible_button_size(canvas_w, canvas_h));
  ImDrawList *dl = ImGui::GetWindowDrawList();

  std::vector<ImVec2> pos(g.nodes.size(), origin);
  for(size_t l = 0; l < by_level.size(); ++l)
  {
    for(size_t i = 0; i < by_level[l].size(); ++i)
    {
      int n = by_level[l][i];
      if(g.direction == Direction::LR || g.direction == Direction::RL)
      {
        pos[(size_t)n] = ImVec2(origin.x + l * (node_w + sx), origin.y + i * (node_h + sy));
      }
      else
      {
        pos[(size_t)n] = ImVec2(origin.x + i * (node_w + sx), origin.y + l * (node_h + sy));
      }
    }
  }

  auto edge_pt = [&](int node, bool out) -> ImVec2 {
    ImVec2 p = pos[(size_t)node];
    if(g.direction == Direction::LR || g.direction == Direction::RL)
      return ImVec2(out ? p.x + node_w : p.x, p.y + node_h * 0.5f);
    return ImVec2(p.x + node_w * 0.5f, out ? p.y + node_h : p.y);
  };

  const ImU32 edge_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  for(const auto &e : g.edges)
  {
    if(e.from < 0 || e.to < 0) continue;
    ImVec2 a = edge_pt(e.from, true);
    ImVec2 b = edge_pt(e.to, false);
    dl->AddLine(a, b, edge_col, 1.5f);

    ImVec2 d = ImVec2(b.x - a.x, b.y - a.y);
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if(len > 1.0f)
    {
      d.x /= len;
      d.y /= len;
      ImVec2 n(-d.y, d.x);
      ImVec2 tip = b;
      ImVec2 l = ImVec2(b.x - d.x * 9.0f + n.x * 4.0f, b.y - d.y * 9.0f + n.y * 4.0f);
      ImVec2 r = ImVec2(b.x - d.x * 9.0f - n.x * 4.0f, b.y - d.y * 9.0f - n.y * 4.0f);
      dl->AddTriangleFilled(tip, l, r, edge_col);
    }
  }

  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
  for(size_t i = 0; i < g.nodes.size(); ++i)
  {
    ImVec2 p = pos[i];
    ImVec2 p2(p.x + node_w, p.y + node_h);
    dl->AddRectFilled(p, p2, fill, 5.0f);
    dl->AddRect(p, p2, border, 5.0f);

    ImVec2 ts = ImGui::CalcTextSize(g.nodes[i].label.c_str());
    ImVec2 tp(p.x + (node_w - ts.x) * 0.5f, p.y + (node_h - ts.y) * 0.5f);
    dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), g.nodes[i].label.c_str());
  }

  ImGui::PopID();
}
} // namespace MermaidFlowchart
