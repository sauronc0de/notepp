// ── treemap_renderer.cpp ───────────────────────────────────────────────────
//
// Treemap diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace treemaprender
{
static ImVec2 tm_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 tm_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace treemaprender

void render_treemap(const TreemapDiagram &d, int id)
{
  using namespace treemaprender;
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float cw = 340.0f, ch = 200.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tm", tm_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);

  std::function<float(int)> total_val = [&](int ni) -> float {
    if(!d.nodes[ni].children.empty())
    {
      float s = 0;
      for(int c : d.nodes[ni].children) s += total_val(c);
      return s;
    }
    return d.nodes[ni].value > 0 ? d.nodes[ni].value : 1.0f;
  };

  std::function<void(int, ImVec2, ImVec2, int)> layout = [&](int ni, ImVec2 tl, ImVec2 br, int depth) {
    auto &node = d.nodes[ni];
    ImU32 fc = tm_series_color(ni + depth * 3, std::max(0.05f, 0.6f - depth * 0.1f));
    dl->AddRectFilled(tl, br, fc, 2.0f);
    dl->AddRect(tl, br, bord, 2.0f, 0, 1.5f);
    float fw = br.x - tl.x;
    float fh = br.y - tl.y;
    std::size_t max_chars = static_cast<std::size_t>(fw / 7.0f);
    std::string lbl = node.name.size() > max_chars ? node.name.substr(0, max_chars) + "…" : node.name;
    ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
    if(ts.x <= fw - 4.0f && ts.y <= fh - 4.0f)
      dl->AddText(ImVec2(tl.x + (fw - ts.x) * 0.5f, tl.y + (fh - ts.y) * 0.5f), tcol, lbl.c_str());
    if(node.children.empty()) return;
    float tot = total_val(ni);
    if(tot <= 0) return;
    float x = tl.x;
    for(int ci : node.children)
    {
      float frac = total_val(ci) / tot;
      float nx = x + fw * frac;
      if(depth % 2 == 0)
        layout(ci, ImVec2(x, tl.y + 16), ImVec2(nx, br.y), depth + 1);
      else
        layout(ci, ImVec2(tl.x, tl.y + 16), ImVec2(br.x, tl.y + 16 + (br.y - tl.y - 16) * frac), depth + 1);
      x = nx;
    }
  };

  std::vector<int> roots;
  for(int i = 0; i < static_cast<int>(d.nodes.size()); ++i)
    if(d.nodes[i].parent < 0) roots.push_back(i);
  if(roots.empty())
  {
    ImGui::Text("(empty treemap)");
    ImGui::PopID();
    return;
  }
  if(roots.size() == 1)
    layout(roots[0], orig, ImVec2(orig.x + cw, orig.y + ch), 0);
  else
  {
    float tot = 0;
    for(int r : roots) tot += total_val(r);
    if(tot <= 0) tot = 1;
    float x = orig.x;
    for(int r : roots)
    {
      float fw2 = cw * total_val(r) / tot;
      layout(r, ImVec2(x, orig.y), ImVec2(x + fw2, orig.y + ch), 0);
      x += fw2;
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
