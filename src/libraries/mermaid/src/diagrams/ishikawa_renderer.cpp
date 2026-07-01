// ── ishikawa_renderer.cpp ──────────────────────────────────────────────────
//
// Ishikawa (fishbone) diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <string>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace ishikawarender
{
static ImVec2 ish_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}
} // namespace ishikawarender

void render_ishikawa(const IshikawaDiagram &d, int id)
{
  using namespace ishikawarender;
  ImGui::PushID(id);
  const float cw = 420.0f, ch = 200.0f, effect_w = 80.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ish", ish_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  float sy = orig.y + ch * 0.5f;
  dl->AddLine(ImVec2(orig.x + 20, sy), ImVec2(orig.x + cw - effect_w - 10, sy), tcol, 2.5f);
  float ex = orig.x + cw - effect_w;
  dl->AddRectFilled(ImVec2(ex, sy - 18), ImVec2(ex + effect_w, sy + 18),
                    ImGui::GetColorU32(ImVec4(0.8f, 0.3f, 0.3f, 0.7f)), 4.0f);
  std::string eff = d.effect.size() > 10 ? d.effect.substr(0, 9) + "…" : d.effect;
  ImVec2 es = ImGui::CalcTextSize(eff.c_str());
  dl->AddText(ImVec2(ex + (effect_w - es.x) * 0.5f, sy - es.y * 0.5f), tcol, eff.c_str());
  int nc = static_cast<int>(d.categories.size());
  if(nc == 0)
  {
    ImGui::PopID();
    return;
  }
  float bone_spacing = (cw - effect_w - 40.0f) / std::max(1, nc);
  for(int i = 0; i < nc; ++i)
  {
    float bx = orig.x + 20.0f + i * bone_spacing + bone_spacing * 0.5f;
    bool top = (i % 2 == 0);
    float ey2 = top ? orig.y + 20 : orig.y + ch - 20;
    dl->AddLine(ImVec2(bx, ey2), ImVec2(bx + (top ? 20 : -20), sy), lcol, 1.5f);
    const std::string &cat = d.categories[i].name;
    ImVec2 cs = ImGui::CalcTextSize(cat.c_str());
    dl->AddText(ImVec2(bx - cs.x * 0.5f, top ? ey2 - cs.y - 2 : ey2 + 2), tcol, cat.c_str());
    for(int j = 0; j < static_cast<int>(d.categories[i].causes.size()) && j < 4; ++j)
    {
      float cx2 = bx + (j + 1) * -12.0f * (top ? -1 : 1);
      float cy2 = top ? sy - (sy - ey2) * ((j + 1) * 0.25f) : sy + (ey2 - sy) * ((j + 1) * 0.25f);
      dl->AddLine(ImVec2(cx2, cy2 - 8), ImVec2(cx2, cy2 + 8), lcol, 1.0f);
      const std::string &cause = d.categories[i].causes[j].text;
      std::string sc = cause.size() > 10 ? cause.substr(0, 9) + "…" : cause;
      ImVec2 ls = ImGui::CalcTextSize(sc.c_str());
      dl->AddText(ImVec2(cx2 - ls.x * 0.5f, top ? cy2 - ls.y - 10 : cy2 + 10), lcol, sc.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
