// ── mindmap_renderer.cpp ───────────────────────────────────────────────────
//
// Mindmap diagram renderer for the mermaid library.
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
namespace mindmaprender
{
static constexpr float kPi = 3.14159265f;

static ImVec2 mindmap_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 mindmap_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace mindmaprender

void render_mindmap(const MindmapDiagram &d, int id)
{
  using namespace mindmaprender;
  if(d.nodes.empty()) return;
  ImGui::PushID(id);
  const float node_r = 44.0f, gap = 14.0f;
  const int n = static_cast<int>(d.nodes.size());
  std::vector<ImVec2> pos(n, ImVec2(0, 0));
  const float canvas_r = std::max(120.0f, (node_r + gap) * (n + 1) * 0.5f);
  float cw = canvas_r * 2.0f + node_r * 2.0f + 20.0f;
  float ch = canvas_r * 2.0f + node_r * 2.0f + 20.0f;
  ImVec2 center(cw * 0.5f, ch * 0.5f);
  pos[0] = center;
  std::vector<bool> placed(n, false);
  placed[0] = true;
  std::function<int(int)> subtree_size = [&](int ni) -> int {
    int s = 1;
    for(int c : d.nodes[ni].children) s += subtree_size(c);
    return s;
  };
  std::function<void(int, float, float, float)> place = [&](int ni, float ax, float span, float dist) {
    auto &children = d.nodes[ni].children;
    int total = 0;
    for(int c : children) total += subtree_size(c);
    float a = ax - span * 0.5f;
    for(int c : children)
    {
      int sz = subtree_size(c);
      float cspan = (total > 0 ? static_cast<float>(sz) / static_cast<float>(total) : 1.0f) * span;
      float ca = a + cspan * 0.5f;
      pos[c] = ImVec2(pos[ni].x + std::cos(ca) * dist, pos[ni].y + std::sin(ca) * dist);
      place(c, ca, cspan, dist * 0.75f);
      a += cspan;
    }
  };
  place(0, 0.0f, 2.0f * kPi, canvas_r * 0.7f);

  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##mm", mindmap_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);

  for(int i = 0; i < n; ++i)
  {
    if(d.nodes[i].parent >= 0)
    {
      ImVec2 a(orig.x + pos[d.nodes[i].parent].x, orig.y + pos[d.nodes[i].parent].y);
      ImVec2 b(orig.x + pos[i].x, orig.y + pos[i].y);
      dl->AddLine(a, b, ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.5f);
    }
  }
  for(int i = 0; i < n; ++i)
  {
    ImVec2 p(orig.x + pos[i].x, orig.y + pos[i].y);
    float r = (i == 0) ? 24.0f : 16.0f;
    ImU32 fc = mindmap_series_color(d.nodes[i].level, 0.7f);
    dl->AddCircleFilled(p, r, fc);
    dl->AddCircle(p, r, bord, 0, 1.5f);
    const std::string &lbl = d.nodes[i].label;
    ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
    std::string disp = lbl;
    if(ts.x > r * 1.8f)
    {
      disp = lbl.substr(0, std::max(1, static_cast<int>(r * 1.8f / ImGui::CalcTextSize("a").x))) + ".";
      ts = ImGui::CalcTextSize(disp.c_str());
    }
    dl->AddText(ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), tcol, disp.c_str());
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
