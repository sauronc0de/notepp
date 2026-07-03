// ── journey_renderer.cpp ───────────────────────────────────────────────────
//
// User journey diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace journeyrender
{
static ImVec2 journey_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 journey_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace journeyrender

void render_journey(const JourneyDiagram &d, int id)
{
  using namespace journeyrender;
  ImGui::PushID(id);
  const float row_h = 28.0f, label_w = 120.0f, score_w = 16.0f, gap = 4.0f;
  int total_tasks = 0;
  for(auto &s : d.sections) total_tasks += static_cast<int>(s.tasks.size());
  float cw = label_w + total_tasks * (score_w + gap) + gap + 100.0f;
  float ch = (static_cast<int>(d.sections.size()) + 1) * row_h + 40.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##jrn", journey_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y + 4.0f), tcol, d.title.c_str());
  }
  float y = orig.y + 30.0f;
  float x0 = orig.x + label_w;
  int sec_idx = 0;
  for(auto &sec : d.sections)
  {
    float x = x0;
    ImU32 sc = journey_series_color(sec_idx++, 0.7f);
    dl->AddRectFilled(ImVec2(orig.x, y), ImVec2(orig.x + label_w - 4.0f, y + row_h), sc, 3.0f);
    ImVec2 ls = ImGui::CalcTextSize(sec.name.c_str());
    dl->AddText(ImVec2(orig.x + 2.0f, y + (row_h - ls.y) * 0.5f), tcol, sec.name.c_str());
    for(auto &t : sec.tasks)
    {
      float bar_h = t.score * 4.0f;
      float by = y + row_h - bar_h;
      dl->AddRectFilled(ImVec2(x, by), ImVec2(x + score_w, y + row_h), sc, 2.0f);
      dl->AddText(ImVec2(x + (score_w - 8.0f) * 0.5f, y + row_h + 2.0f),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  std::to_string(t.score).c_str());
      x += score_w + gap;
    }
    y += row_h;
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
