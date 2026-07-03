// ── timeline_renderer.cpp ──────────────────────────────────────────────────
//
// Timeline diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace timelinerender
{
static ImVec2 tl_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 tl_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}

static ImVec2 tl_center_text(ImVec2 p, ImVec2 sz, const std::string &t)
{
  ImVec2 ts = ImGui::CalcTextSize(t.c_str());
  return ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f);
}

static ImVec4 tl_draw_box(ImDrawList *dl, ImVec2 p, ImVec2 sz, ImU32 fill, ImU32 border,
                          const std::string &label, float rounding = 4.0f)
{
  ImVec2 p2(p.x + sz.x, p.y + sz.y);
  dl->AddRectFilled(p, p2, fill, rounding);
  dl->AddRect(p, p2, border, rounding);
  ImVec2 tp = tl_center_text(p, sz, label);
  dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
  return ImVec4(p.x, p.y, p2.x, p2.y);
}
} // namespace timelinerender

void render_timeline(const TimelineDiagram &d, int id)
{
  using namespace timelinerender;
  ImGui::PushID(id);
  const float period_w = 100.0f, event_h = 22.0f, period_h = 32.0f, hgap = 6.0f, pad = 12.0f;
  int np = static_cast<int>(d.periods.size());
  if(np == 0)
  {
    ImGui::Text("(empty timeline)");
    ImGui::PopID();
    return;
  }
  int max_events = 0;
  for(auto &p : d.periods) max_events = std::max(max_events, static_cast<int>(p.events.size()));
  float cw = static_cast<float>(np) * (period_w + hgap) + hgap + pad * 2.0f;
  float ch = pad * 2.0f + period_h + max_events * event_h + 20.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##tl", tl_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y + 2.0f), tcol, d.title.c_str());
  }
  float axis_y = orig.y + pad + 20.0f + period_h * 0.5f;
  dl->AddLine(ImVec2(orig.x + pad, axis_y), ImVec2(orig.x + pad + np * (period_w + hgap), axis_y), lcol, 2.0f);
  for(int i = 0; i < np; ++i)
  {
    float x = orig.x + pad + i * (period_w + hgap);
    ImU32 pc = tl_series_color(i, 0.8f);
    tl_draw_box(dl, ImVec2(x, orig.y + pad + 20.0f), ImVec2(period_w, period_h),
                ImGui::GetColorU32(ImGuiCol_FrameBg), pc, d.periods[i].label);
    dl->AddLine(ImVec2(x + period_w * 0.5f, orig.y + pad + 20.0f + period_h),
                ImVec2(x + period_w * 0.5f, axis_y), pc, 1.5f);
    float ey = orig.y + pad + 20.0f + period_h + 4.0f;
    for(auto &ev : d.periods[i].events)
    {
      std::string short_ev = ev.size() > 14 ? ev.substr(0, 13) + "…" : ev;
      tl_draw_box(dl, ImVec2(x, ey), ImVec2(period_w, event_h - 2.0f),
                  ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), pc, short_ev, 2.0f);
      ey += event_h;
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
