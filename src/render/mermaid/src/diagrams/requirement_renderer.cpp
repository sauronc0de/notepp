// ── requirement_renderer.cpp ──────────────────────────────────────────────
//
// Requirement diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace requirementrender
{
static ImVec2 req_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static std::pair<int, int> req_grid_pos(int i, int cols)
{
  return {i % cols, i / cols};
}

static ImVec2 req_rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  float dx = other.x - cen.x;
  float dy = other.y - cen.y;
  if(std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) return cen;
  float tx = hw / std::abs(dx);
  float ty = hh / std::abs(dy);
  float t = std::min(tx, ty);
  return ImVec2(cen.x + dx * t, cen.y + dy * t);
}

static void req_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open = false)
{
  ImVec2 n(-dir.y, dir.x);
  ImVec2 l(tip.x - dir.x * sz + n.x * (sz * 0.5f), tip.y - dir.y * sz + n.y * (sz * 0.5f));
  ImVec2 r(tip.x - dir.x * sz - n.x * (sz * 0.5f), tip.y - dir.y * sz - n.y * (sz * 0.5f));
  if(open)
  {
    dl->AddLine(l, tip, col, 1.5f);
    dl->AddLine(r, tip, col, 1.5f);
  }
  else
  {
    dl->AddTriangleFilled(tip, l, r, col);
  }
}
} // namespace requirementrender

void render_requirement(const RequirementDiagram &d, int id)
{
  using namespace requirementrender;
  ImGui::PushID(id);
  const float bw = 180.0f, bh = 80.0f, hgap = 40.0f, vgap = 20.0f;
  int n = static_cast<int>(d.reqs.size()) + static_cast<int>(d.elements.size());
  if(n == 0)
  {
    ImGui::Text("(empty requirement diagram)");
    ImGui::PopID();
    return;
  }
  int cols = std::min(3, n);
  int rows = (n + cols - 1) / cols;
  float cw = cols * (bw + hgap) + hgap;
  float ch = rows * (bh + vgap) + vgap;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##req", req_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 hfill = ImGui::GetColorU32(ImGuiCol_TitleBg);

  std::vector<ImVec2> centers(n);
  std::vector<std::string> node_names(n);
  int idx = 0;
  auto draw_node = [&](int i, const std::string &type, const std::string &name, const std::string &detail) {
    auto [col, row] = req_grid_pos(i, cols);
    float x = orig.x + hgap + col * (bw + hgap);
    float y = orig.y + vgap + row * (bh + vgap);
    centers[i] = ImVec2(x + bw * 0.5f, y + bh * 0.5f);
    node_names[i] = name;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), fill, 4.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh), bord, 4.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + 20.0f), hfill, 4.0f);
    ImVec2 ts = ImGui::CalcTextSize(type.c_str());
    dl->AddText(ImVec2(x + (bw - ts.x) * 0.5f, y + 2.0f), lcol, type.c_str());
    ImVec2 ns = ImGui::CalcTextSize(name.c_str());
    dl->AddText(ImVec2(x + (bw - ns.x) * 0.5f, y + 22.0f), tcol, name.c_str());
    if(!detail.empty())
    {
      std::string d2 = detail.size() > 22 ? detail.substr(0, 21) + "…" : detail;
      ImVec2 ds = ImGui::CalcTextSize(d2.c_str());
      dl->AddText(ImVec2(x + (bw - ds.x) * 0.5f, y + 42.0f), lcol, d2.c_str());
    }
  };
  for(auto &r : d.reqs)
  {
    draw_node(idx, r.type, r.name, r.text);
    idx++;
  }
  for(auto &e : d.elements)
  {
    draw_node(idx, "element", e.name, e.type);
    idx++;
  }
  auto find_node = [&](const std::string &name) -> int {
    for(int i = 0; i < n; ++i)
      if(node_names[i] == name) return i;
    return -1;
  };
  for(auto &r : d.relations)
  {
    int fi = find_node(r.from);
    int ti = find_node(r.to);
    if(fi < 0 || ti < 0) continue;
    ImVec2 a = req_rect_edge(centers[fi], bw * 0.5f, bh * 0.5f, centers[ti]);
    ImVec2 b = req_rect_edge(centers[ti], bw * 0.5f, bh * 0.5f, centers[fi]);
    dl->AddLine(a, b, lcol, 1.5f);
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len > 1.0f)
    {
      dx /= len;
      dy /= len;
    }
    req_draw_arrow_head(dl, b, ImVec2(dx, dy), 8.0f, lcol);
    if(!r.reltype.empty())
    {
      ImVec2 ts = ImGui::CalcTextSize(r.reltype.c_str());
      dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y - 2.0f),
                  tcol, r.reltype.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
