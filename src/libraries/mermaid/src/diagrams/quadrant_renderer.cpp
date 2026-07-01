// ── quadrant_renderer.cpp ──────────────────────────────────────────────────
//
// Quadrant chart diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace quadrantrender
{
static ImVec2 quadrant_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 quadrant_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace quadrantrender

void render_quadrant(const QuadrantDiagram &d, int id)
{
  using namespace quadrantrender;
  ImGui::PushID(id);
  const float sz = 260.0f, pad = 40.0f;
  float cw = sz + pad * 2.0f;
  float ch = sz + pad * 2.0f + 20.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##quad", quadrant_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 gc = ImGui::GetColorU32(ImGuiCol_Separator);
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y + 2.0f), tcol, d.title.c_str());
  }
  ImVec2 tl(orig.x + pad, orig.y + 20.0f + pad);
  ImVec2 br(tl.x + sz, tl.y + sz);
  ImVec2 mid(tl.x + sz * 0.5f, tl.y + sz * 0.5f);
  dl->AddRectFilled(tl, mid, ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 0.2f, 0.12f)));
  dl->AddRectFilled(ImVec2(mid.x, tl.y), ImVec2(br.x, mid.y),
                    ImGui::GetColorU32(ImVec4(0.6f, 0.2f, 0.2f, 0.12f)));
  dl->AddRectFilled(ImVec2(tl.x, mid.y), mid,
                    ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.6f, 0.12f)));
  dl->AddRectFilled(mid, br, ImGui::GetColorU32(ImVec4(0.6f, 0.6f, 0.2f, 0.12f)));
  dl->AddRect(tl, br, gc, 0, 0, 1.5f);
  dl->AddLine(ImVec2(mid.x, tl.y), ImVec2(mid.x, br.y), gc, 1.0f);
  dl->AddLine(ImVec2(tl.x, mid.y), ImVec2(br.x, mid.y), gc, 1.0f);
  auto ql = [&](float x, float y, const std::string &s) {
    if(!s.empty())
    {
      ImVec2 ts = ImGui::CalcTextSize(s.c_str());
      dl->AddText(ImVec2(x - ts.x * 0.5f, y - ts.y * 0.5f), lcol, s.c_str());
    }
  };
  ql(tl.x + sz * 0.25f, tl.y + sz * 0.25f, d.q2);
  ql(tl.x + sz * 0.75f, tl.y + sz * 0.25f, d.q1);
  ql(tl.x + sz * 0.25f, tl.y + sz * 0.75f, d.q3);
  ql(tl.x + sz * 0.75f, tl.y + sz * 0.75f, d.q4);
  if(!d.x_low.empty()) { ImVec2 ts = ImGui::CalcTextSize(d.x_low.c_str()); dl->AddText(ImVec2(tl.x, br.y + 4.0f), lcol, d.x_low.c_str()); }
  if(!d.x_high.empty()) { ImVec2 ts = ImGui::CalcTextSize(d.x_high.c_str()); dl->AddText(ImVec2(br.x - ts.x, br.y + 4.0f), lcol, d.x_high.c_str()); }
  if(!d.y_low.empty()) { ImVec2 ts = ImGui::CalcTextSize(d.y_low.c_str()); dl->AddText(ImVec2(tl.x - ts.x - 4.0f, br.y - ts.y), lcol, d.y_low.c_str()); }
  if(!d.y_high.empty()) { ImVec2 ts = ImGui::CalcTextSize(d.y_high.c_str()); dl->AddText(ImVec2(tl.x - ts.x - 4.0f, tl.y), lcol, d.y_high.c_str()); }
  for(int i = 0; i < static_cast<int>(d.points.size()); ++i)
  {
    auto &p = d.points[i];
    float px = tl.x + p.x * sz;
    float py = br.y - p.y * sz;
    ImU32 pc = quadrant_series_color(i);
    dl->AddCircleFilled(ImVec2(px, py), 5.0f, pc);
    dl->AddText(ImVec2(px + 7.0f, py - 8.0f), tcol, p.name.c_str());
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
