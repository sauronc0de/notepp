// ── er_renderer.cpp ────────────────────────────────────────────────────────
//
// ER (entity-relationship) diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace errender
{
static ImVec2 er_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static std::pair<int, int> er_grid_pos(int i, int cols)
{
  return {(i % cols + cols) % cols, i / cols};
}

static ImVec2 er_rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  const float dx = other.x - cen.x;
  const float dy = other.y - cen.y;
  if(dx == 0.0f && dy == 0.0f) return cen;
  const float tx = (dx == 0.0f) ? 1e6f : hw / std::abs(dx);
  const float ty = (dy == 0.0f) ? 1e6f : hh / std::abs(dy);
  const float t = std::min(tx, ty);
  return ImVec2(cen.x + dx * t, cen.y + dy * t);
}
} // namespace errender

void render_er(const ERDiagram &d, int id)
{
  using namespace errender;
  if(d.entities.empty()) return;
  ImGui::PushID(id);
  const float ew = 160.0f, header_h = 24.0f, row_h = 18.0f, hgap = 50.0f, vgap = 20.0f;
  int n = static_cast<int>(d.entities.size());
  int cols = std::min(3, n);
  int rows = (n + cols - 1) / cols;

  std::vector<float> heights(n);
  float maxh = 0.0f;
  for(int i = 0; i < n; ++i)
  {
    heights[i] = header_h + 4.0f + d.entities[i].attrs.size() * row_h;
    maxh = std::max(maxh, heights[i]);
  }
  float cw = cols * (ew + hgap) + hgap;
  float ch = rows * (maxh + vgap) + vgap;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##er", er_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hfill = ImGui::GetColorU32(ImGuiCol_TitleBg);
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  std::vector<ImVec2> centers(n);
  std::vector<float> er_half_h(n);
  for(int i = 0; i < n; ++i)
  {
    auto [col, row] = er_grid_pos(i, cols);
    float x = orig.x + hgap + col * (ew + hgap);
    float y = orig.y + vgap + row * (maxh + vgap);
    float h = heights[i];
    centers[i] = ImVec2(x + ew * 0.5f, y + h * 0.5f);
    er_half_h[i] = h * 0.5f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ew, y + h), fill, 3.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + ew, y + h), bord, 3.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ew, y + header_h), hfill, 3.0f);
    auto &e = d.entities[i];
    ImVec2 ns = ImGui::CalcTextSize(e.name.c_str());
    dl->AddText(ImVec2(x + (ew - ns.x) * 0.5f, y + (header_h - ns.y) * 0.5f), tcol, e.name.c_str());
    float ay = y + header_h + 2.0f;
    dl->AddLine(ImVec2(x, ay), ImVec2(x + ew, ay), bord);
    for(auto &a : e.attrs)
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%s%s %s", a.pk ? "PK " : a.fk ? "FK " : "", a.type.c_str(), a.name.c_str());
      ImU32 ac = a.pk ? ImGui::GetColorU32(ImVec4(1, 0.8f, 0.3f, 1)) : tcol;
      dl->AddText(ImVec2(x + 4.0f, ay + 2.0f), ac, buf);
      ay += row_h;
    }
  }

  auto find_idx = [&](const std::string &name) -> int {
    for(int i = 0; i < n; ++i)
      if(d.entities[i].name == name) return i;
    return -1;
  };
  for(auto &r : d.relations)
  {
    int fi = find_idx(r.e1);
    int ti = find_idx(r.e2);
    if(fi < 0 || ti < 0) continue;
    ImVec2 a = er_rect_edge(centers[fi], ew * 0.5f, er_half_h[fi], centers[ti]);
    ImVec2 b = er_rect_edge(centers[ti], ew * 0.5f, er_half_h[ti], centers[fi]);
    dl->AddLine(a, b, lcol, 1.5f);
    if(!r.label.empty())
    {
      ImVec2 ts = ImGui::CalcTextSize(r.label.c_str());
      dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y - 2.0f), tcol, r.label.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
