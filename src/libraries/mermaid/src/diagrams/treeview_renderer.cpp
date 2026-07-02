// ── treeview_renderer.cpp ──────────────────────────────────────────────────
//
// Treeview diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace treeviewrender
{
static ImVec2 tv_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 tv_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace treeviewrender

void render_treeview(const TreeViewDiagram &d, int id)
{
  using namespace treeviewrender;
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float row_h = 22.0f, indent_w = 18.0f, pad = 8.0f;
  int n = static_cast<int>(d.nodes.size());
  float max_depth = 0;
  for(auto &nd : d.nodes)
  {
    int dep = 0;
    int p = nd.parent;
    while(p >= 0) { dep++; p = d.nodes[p].parent; }
    if(static_cast<float>(dep) > max_depth) max_depth = static_cast<float>(dep);
  }
  float cw = pad + max_depth * indent_w + 200.0f;
  float ch = static_cast<float>(n) * row_h + pad * 2.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tv", tv_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  std::function<void(int, int, int &)> draw_node = [&](int ni, int depth, int &row) {
    float x = orig.x + pad + static_cast<float>(depth) * indent_w;
    float y = orig.y + pad + static_cast<float>(row) * row_h;
    if(depth > 0)
    {
      dl->AddLine(ImVec2(x - indent_w + 6, y + row_h * 0.5f), ImVec2(x, y + row_h * 0.5f), lcol, 1.0f);
      dl->AddLine(ImVec2(x - indent_w + 6, y - row_h * 0.5f), ImVec2(x - indent_w + 6, y + row_h * 0.5f), lcol, 1.0f);
    }
    bool has_children = !d.nodes[ni].children.empty();
    if(has_children)
      dl->AddTriangleFilled(ImVec2(x, y + row_h * 0.5f - 4),
                             ImVec2(x, y + row_h * 0.5f + 4),
                             ImVec2(x + 6, y + row_h * 0.5f),
                             tv_series_color(depth));
    else
      dl->AddCircleFilled(ImVec2(x + 3, y + row_h * 0.5f), 3.0f, tv_series_color(depth, 0.7f));
    dl->AddText(ImVec2(x + 10, y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), tcol, d.nodes[ni].label.c_str());
    row++;
    for(int c : d.nodes[ni].children) draw_node(c, depth + 1, row);
  };
  int row = 0;
  for(int i = 0; i < n; ++i) if(d.nodes[i].parent < 0) draw_node(i, 0, row);
  ImGui::PopID();
}
} // namespace MermaidDiagrams
