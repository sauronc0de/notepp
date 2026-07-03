// ── gantt_renderer.cpp ─────────────────────────────────────────────────────
//
// Gantt diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace ganttrender
{
static ImVec2 gantt_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 gantt_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace ganttrender

void render_gantt(const GanttDiagram &d, int id)
{
  using namespace ganttrender;
  ImGui::PushID(id);
  struct FlatTask
  {
    std::string name;
    int start, dur;
    bool crit, milestone;
    int sec_idx;
  };
  std::vector<FlatTask> flat;
  std::unordered_map<std::string, int> id_end_day;
  int max_day = 0;
  for(int si = 0; si < static_cast<int>(d.sections.size()); ++si)
  {
    for(auto &t : d.sections[si].tasks)
    {
      int start = t.start_day;
      if(!t.after.empty())
      {
        auto it = id_end_day.find(t.after);
        if(it != id_end_day.end()) start = it->second;
      }
      if(!t.id.empty()) id_end_day[t.id] = start + t.dur;
      flat.push_back({t.name, start, t.dur, t.is_crit, t.is_milestone, si});
      max_day = std::max(max_day, start + t.dur);
    }
  }
  if(max_day == 0) max_day = 10;
  const float label_w = 130.0f, row_h = 22.0f, axis_h = 24.0f;
  const float bar_area = 360.0f;
  float px_per_day = bar_area / static_cast<float>(max_day);
  float cw = label_w + bar_area + 8.0f;
  float ch = axis_h + (flat.size() + static_cast<int>(d.sections.size())) * row_h + 8.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##gantt", gantt_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y), tcol, d.title.c_str());
  }
  float y = orig.y + 20.0f;
  dl->AddLine(ImVec2(orig.x + label_w, y + axis_h), ImVec2(orig.x + label_w + bar_area, y + axis_h), lcol, 1.5f);
  int tick_step = std::max(1, max_day / 8);
  for(int t = 0; t <= max_day; t += tick_step)
  {
    float tx = orig.x + label_w + t * px_per_day;
    dl->AddLine(ImVec2(tx, y + axis_h - 4.0f), ImVec2(tx, y + axis_h + 4.0f), lcol, 1.0f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", t);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(tx - ts.x * 0.5f, y), lcol, buf);
  }
  y += axis_h;
  int si = -1;
  for(auto &ft : flat)
  {
    if(ft.sec_idx != si)
    {
      si = ft.sec_idx;
      const std::string &sname = d.sections[si].name;
      if(!sname.empty())
      {
        dl->AddRectFilled(ImVec2(orig.x, y), ImVec2(orig.x + cw, y + row_h),
                          gantt_series_color(si, 0.18f), 0.0f);
        dl->AddText(ImVec2(orig.x + 2.0f, y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    tcol, sname.c_str());
        y += row_h;
      }
    }
    float x0 = orig.x + label_w + ft.start * px_per_day;
    float bw = ft.dur * px_per_day;
    ImU32 bc = ft.crit ? ImGui::GetColorU32(ImVec4(0.9f, 0.3f, 0.3f, 0.8f))
                       : gantt_series_color(ft.sec_idx, 0.75f);
    std::string lbl = ft.name.size() > 16 ? ft.name.substr(0, 15) + "…" : ft.name;
    ImVec2 ls = ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(orig.x + label_w - ls.x - 4.0f, y + (row_h - ls.y) * 0.5f), tcol, lbl.c_str());
    if(ft.milestone)
    {
      float mx = x0 + bw * 0.5f;
      float my = y + row_h * 0.5f;
      dl->AddTriangleFilled(ImVec2(mx, my - 8.0f), ImVec2(mx + 8.0f, my), ImVec2(mx - 8.0f, my), bc);
      dl->AddTriangleFilled(ImVec2(mx, my + 8.0f), ImVec2(mx + 8.0f, my), ImVec2(mx - 8.0f, my), bc);
    }
    else
    {
      dl->AddRectFilled(ImVec2(x0, y + 3.0f), ImVec2(x0 + std::max(2.0f, bw), y + row_h - 3.0f), bc, 3.0f);
    }
    y += row_h;
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
