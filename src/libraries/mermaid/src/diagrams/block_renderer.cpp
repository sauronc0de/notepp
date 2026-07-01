// ── block_renderer.cpp ─────────────────────────────────────────────────────
//
// Block diagram renderer for the mermaid library.
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
namespace blockrender
{
static ImVec2 blk_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static std::pair<int, int> blk_grid_pos(int i, int cols)
{
  return {i % cols, i / cols};
}

static ImVec2 blk_rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  float dx = other.x - cen.x;
  float dy = other.y - cen.y;
  if(std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) return cen;
  float tx = hw / std::abs(dx);
  float ty = hh / std::abs(dy);
  float t = std::min(tx, ty);
  return ImVec2(cen.x + dx * t, cen.y + dy * t);
}

static void blk_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open = false)
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
} // namespace blockrender

void render_block(const BlockDiagram &d, int id)
{
  using namespace blockrender;
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float nw = 100.0f, nh = 32.0f, hgap = 40.0f, vgap = 20.0f;
  int n = static_cast<int>(d.nodes.size());
  int cols = d.columns > 0 ? d.columns : std::min(4, n);
  int rows = (n + cols - 1) / cols;
  float cw = cols * (nw + hgap) + hgap;
  float ch = rows * (nh + vgap) + vgap;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##blk", blk_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  std::vector<ImVec2> centers(n);
  for(int i = 0; i < n; ++i)
  {
    auto [col, row] = blk_grid_pos(i, cols);
    float x = orig.x + hgap + col * (nw + hgap);
    float y = orig.y + vgap + row * (nh + vgap);
    centers[i] = ImVec2(x + nw * 0.5f, y + nh * 0.5f);
    float rounding = d.nodes[i].shape == "round" ? nh * 0.5f : 4.0f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + nw, y + nh), fill, rounding);
    dl->AddRect(ImVec2(x, y), ImVec2(x + nw, y + nh), bord, rounding);
    ImVec2 ts = ImGui::CalcTextSize(d.nodes[i].label.c_str());
    dl->AddText(ImVec2(x + (nw - ts.x) * 0.5f, y + (nh - ts.y) * 0.5f), tcol, d.nodes[i].label.c_str());
  }
  auto find_idx = [&](const std::string &sid) -> int {
    for(int i = 0; i < n; ++i)
      if(d.nodes[i].id == sid) return i;
    return -1;
  };
  for(auto &e : d.edges)
  {
    int fi = find_idx(e.from);
    int ti = find_idx(e.to);
    if(fi < 0 || ti < 0) continue;
    ImVec2 a = blk_rect_edge(centers[fi], nw * 0.5f, nh * 0.5f, centers[ti]);
    ImVec2 b = blk_rect_edge(centers[ti], nw * 0.5f, nh * 0.5f, centers[fi]);
    dl->AddLine(a, b, lcol, 1.5f);
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len > 1)
    {
      dx /= len;
      dy /= len;
    }
    blk_draw_arrow_head(dl, b, ImVec2(dx, dy), 8.0f, lcol);
    if(!e.label.empty())
    {
      ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
      dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y - 2.0f), tcol, e.label.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
