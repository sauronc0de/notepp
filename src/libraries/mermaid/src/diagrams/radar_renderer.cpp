// ── radar_renderer.cpp ─────────────────────────────────────────────────────
//
// Radar chart diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace radarrender
{
static constexpr float kPi = 3.14159265f;

static ImVec2 radar_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 radar_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace radarrender

void render_radar(const RadarDiagram &d, int id)
{
  using namespace radarrender;
  ImGui::PushID(id);
  const float r = 110.0f, pad = 60.0f;
  float cw = r * 2.0f + pad * 2.0f;
  float ch = r * 2.0f + pad * 2.0f + 20.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##radar", radar_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol = ImGui::GetColorU32(ImGuiCol_Separator);
  ImVec2 center(orig.x + pad + r, orig.y + 20.0f + pad + r);
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y + 2.0f), tcol, d.title.c_str());
  }
  int na = static_cast<int>(d.axes.size());
  if(na < 3)
  {
    ImGui::Text("Need >= 3 axes");
    ImGui::PopID();
    return;
  }
  for(int level = 1; level <= 4; ++level)
  {
    float lr = r * level * 0.25f;
    std::vector<ImVec2> pts;
    for(int a = 0; a <= na; ++a)
    {
      float angle = -kPi * 0.5f + a * (2.0f * kPi / na);
      pts.push_back(ImVec2(center.x + std::cos(angle) * lr, center.y + std::sin(angle) * lr));
    }
    for(int a = 0; a < na; ++a) dl->AddLine(pts[a], pts[a + 1], gcol, 1.0f);
  }
  for(int a = 0; a < na; ++a)
  {
    float angle = -kPi * 0.5f + a * (2.0f * kPi / na);
    ImVec2 tip(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
    dl->AddLine(center, tip, gcol, 1.0f);
    const std::string &lbl = d.axes[a];
    ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
    float lx = center.x + std::cos(angle) * (r + 14.0f) - ts.x * 0.5f;
    float ly = center.y + std::sin(angle) * (r + 14.0f) - ts.y * 0.5f;
    dl->AddText(ImVec2(lx, ly), tcol, lbl.c_str());
  }
  float max_v = d.max_val > 0 ? d.max_val : 100.0f;
  for(int ci = 0; ci < static_cast<int>(d.curves.size()); ++ci)
  {
    auto &c = d.curves[ci];
    if(c.values.empty()) continue;
    std::vector<ImVec2> pts;
    for(int a = 0; a < na; ++a)
    {
      float v = a < static_cast<int>(c.values.size()) ? c.values[a] : 0.0f;
      float frac = v / max_v;
      float angle = -kPi * 0.5f + a * (2.0f * kPi / na);
      pts.push_back(ImVec2(center.x + std::cos(angle) * r * frac, center.y + std::sin(angle) * r * frac));
    }
    ImU32 cc = radar_series_color(ci, 0.35f);
    ImU32 cc2 = radar_series_color(ci, 0.85f);
    dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), cc);
    pts.push_back(pts[0]);
    for(int k = 0; k < static_cast<int>(pts.size()) - 1; ++k) dl->AddLine(pts[k], pts[k + 1], cc2, 2.0f);
    if(!c.name.empty())
    {
      dl->AddText(ImVec2(orig.x + pad + static_cast<float>(ci) * 60.0f, orig.y + cw - 20), cc2, c.name.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
