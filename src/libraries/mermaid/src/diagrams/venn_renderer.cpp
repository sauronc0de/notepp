// ── venn_renderer.cpp ─────────────────────────────────────────────────────
//
// Venn diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace vennrender
{
static ImVec2 venn_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}
} // namespace vennrender

void render_venn(const VennDiagram &d, int id)
{
  using namespace vennrender;
  ImGui::PushID(id);
  int ns = static_cast<int>(d.sets.size());
  if(ns < 2)
  {
    ImGui::Text("Need >= 2 sets for Venn");
    ImGui::PopID();
    return;
  }
  const float r = 58.0f, pad = 20.0f, title_h = 20.0f;

  float cw, ch;
  if(ns == 3)
  {
    cw = r * 3.8f + pad * 2.0f;
    ch = r * 3.2f + pad * 2.0f + title_h;
  }
  else
  {
    float overlap = r * 0.38f;
    cw = static_cast<float>(ns) * r * 2.0f - (ns - 1) * overlap + pad * 2.0f;
    ch = r * 2.0f + pad * 2.0f + title_h + 20.0f;
  }

  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##venn", venn_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);

  std::vector<ImVec2> set_centers(ns);
  if(ns == 3)
  {
    float cx = orig.x + cw * 0.5f;
    float cy = orig.y + title_h + pad + r * 1.15f;
    float ox = r * 0.78f;
    float oy = r * 0.45f;
    set_centers[0] = ImVec2(cx, cy - oy);
    set_centers[1] = ImVec2(cx - ox, cy + oy);
    set_centers[2] = ImVec2(cx + ox, cy + oy);
  }
  else
  {
    float overlap = r * 0.38f;
    float start_x = orig.x + pad + r;
    float cy = orig.y + title_h + pad + r;
    for(int i = 0; i < ns; ++i) set_centers[i] = ImVec2(start_x + i * (r * 2.0f - overlap), cy);
  }

  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y), tcol, d.title.c_str());
  }

  for(int i = 0; i < ns; ++i)
  {
    float rr, gg, bb;
    ImGui::ColorConvertHSVtoRGB(static_cast<float>(i) / static_cast<float>(ns), 0.55f, 0.9f, rr, gg, bb);
    dl->AddCircleFilled(set_centers[i], r, ImGui::GetColorU32(ImVec4(rr, gg, bb, 0.22f)));
    dl->AddCircle(set_centers[i], r, ImGui::GetColorU32(ImVec4(rr, gg, bb, 0.8f)), 0, 2.0f);
  }

  for(int i = 0; i < ns; ++i)
  {
    std::string lbl = d.sets[i].label;
    ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
    ImVec2 lp;
    if(ns == 3)
    {
      if(i == 0) lp = ImVec2(set_centers[0].x - ts.x * 0.5f, set_centers[0].y - r - ts.y - 2);
      else if(i == 1) lp = ImVec2(set_centers[1].x - ts.x - r * 0.15f, set_centers[1].y + r * 0.55f);
      else lp = ImVec2(set_centers[2].x + r * 0.15f, set_centers[2].y + r * 0.55f);
    }
    else
    {
      lp = ImVec2(set_centers[i].x - ts.x * 0.5f, set_centers[i].y + r + 4);
    }
    dl->AddText(lp, tcol, lbl.c_str());
  }

  for(auto &vi : d.intersections)
  {
    if(vi.label.empty() || vi.set_ids.size() < 2) continue;
    float ix = 0, iy = 0;
    int cnt = 0;
    for(auto &sid : vi.set_ids)
    {
      for(int i = 0; i < ns; ++i)
      {
        if(d.sets[i].id == sid) { ix += set_centers[i].x; iy += set_centers[i].y; cnt++; break; }
      }
    }
    if(cnt > 0)
    {
      ix /= cnt;
      iy /= cnt;
      ImVec2 ts = ImGui::CalcTextSize(vi.label.c_str());
      dl->AddText(ImVec2(ix - ts.x * 0.5f, iy - ts.y * 0.5f), tcol, vi.label.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
