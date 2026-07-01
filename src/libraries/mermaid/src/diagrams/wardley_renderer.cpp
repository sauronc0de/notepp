// ── wardley_renderer.cpp ──────────────────────────────────────────────────
//
// Wardley map diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace wardleyrender
{
static ImVec2 wd_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 wd_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}

static void wd_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open = false)
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
} // namespace wardleyrender

void render_wardley(const WardleyDiagram &d, int id)
{
  using namespace wardleyrender;
  ImGui::PushID(id);
  const float cw = 320.0f, ch = 240.0f, pad = 40.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##wd", wd_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol = ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  float pw = cw - pad * 2.0f;
  float ph = ch - pad * 2.0f - 20.0f;
  ImVec2 tl(orig.x + pad, orig.y + 20.0f + pad);
  dl->AddLine(ImVec2(tl.x, tl.y), ImVec2(tl.x, tl.y + ph), tcol, 1.5f);
  dl->AddLine(ImVec2(tl.x, tl.y + ph), ImVec2(tl.x + pw, tl.y + ph), tcol, 1.5f);
  dl->AddText(ImVec2(tl.x - 2, tl.y - 14), lcol, "Visible");
  dl->AddText(ImVec2(tl.x - 2, tl.y + ph + 2), lcol, "Invisible");
  const char *stages[] = {"Genesis", "Custom", "Product", "Commodity"};
  for(int s = 0; s < 4; ++s)
  {
    ImVec2 ts = ImGui::CalcTextSize(stages[s]);
    dl->AddText(ImVec2(tl.x + pw * s / 3.0f - ts.x * 0.5f, tl.y + ph + 14), lcol, stages[s]);
  }
  for(int s = 1; s < 4; ++s)
  {
    float gx = tl.x + pw * s / 4.0f;
    dl->AddLine(ImVec2(gx, tl.y), ImVec2(gx, tl.y + ph), gcol, 1.0f);
  }
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y), tcol, d.title.c_str());
  }
  std::unordered_map<std::string, ImVec2> comp_pos;
  for(int i = 0; i < static_cast<int>(d.components.size()); ++i)
  {
    auto &c = d.components[i];
    float px = tl.x + c.evolution * pw;
    float py = tl.y + (1.0f - c.visibility) * ph;
    comp_pos[c.name] = ImVec2(px, py);
    ImU32 cc = wd_series_color(i);
    dl->AddCircleFilled(ImVec2(px, py), 6.0f, cc);
    ImVec2 ts = ImGui::CalcTextSize(c.name.c_str());
    dl->AddText(ImVec2(px - ts.x * 0.5f, py - ts.y - 4), tcol, c.name.c_str());
  }
  for(auto &l : d.links)
  {
    auto ai = comp_pos.find(l.from);
    auto bi = comp_pos.find(l.to);
    if(ai == comp_pos.end() || bi == comp_pos.end()) continue;
    dl->AddLine(ai->second, bi->second, lcol, 1.5f);
    float dx = bi->second.x - ai->second.x;
    float dy = bi->second.y - ai->second.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len > 1)
    {
      dx /= len;
      dy /= len;
    }
    wd_draw_arrow_head(dl, bi->second, ImVec2(dx, dy), 7.0f, lcol);
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
