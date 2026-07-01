// ── architecture_renderer.cpp ──────────────────────────────────────────────
//
// Architecture diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace archrender
{
static ImVec2 arch_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 arch_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}

static std::pair<int, int> arch_grid_pos(int i, int cols)
{
  return {i % cols, i / cols};
}

static void arch_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open = false)
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
} // namespace archrender

void render_architecture(const ArchDiagram &d, int id)
{
  using namespace archrender;
  ImGui::PushID(id);
  const float sw2 = 110.0f, sh = 36.0f, hgap = 40.0f, vgap = 20.0f, gpad = 12.0f;
  int ns = static_cast<int>(d.services.size());
  if(ns == 0) ns = 1;
  int cols = std::min(4, ns);
  int rows = (ns + cols - 1) / cols;
  float cw = cols * (sw2 + hgap) + hgap + gpad * 2.0f;
  float ch = rows * (sh + vgap) + vgap + gpad * 2.0f + 20.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##arch", arch_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  for(int i = 0; i < static_cast<int>(d.groups.size()); ++i)
  {
    float gx = orig.x + gpad + i * (sw2 + hgap) * 0.5f;
    float gy = orig.y + 20.0f;
    float gw = std::min(cw - gpad * 2.0f, static_cast<float>(cols) * (sw2 + hgap));
    float gh = ch - 40.0f;
    dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh),
                      arch_series_color(i + 4, 0.08f), 6.0f);
    dl->AddRect(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh),
                arch_series_color(i + 4, 0.4f), 6.0f, 0, 1.5f);
    dl->AddText(ImVec2(gx + 4, gy + 2), arch_series_color(i + 4, 1.0f), d.groups[i].label.c_str());
  }
  std::unordered_map<std::string, ImVec2> scenters;
  for(int i = 0; i < static_cast<int>(d.services.size()); ++i)
  {
    auto [col, row] = arch_grid_pos(i, cols);
    float x = orig.x + gpad + hgap + col * (sw2 + hgap);
    float y = orig.y + 20.0f + gpad + vgap + row * (sh + vgap);
    scenters[d.services[i].id] = ImVec2(x + sw2 * 0.5f, y + sh * 0.5f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sw2, y + sh), fill, 5.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + sw2, y + sh), bord, 5.0f);
    std::string disp = d.services[i].label;
    if(!d.services[i].icon.empty()) disp = "[" + d.services[i].icon + "] " + disp;
    std::string short_d = disp.size() > 14 ? disp.substr(0, 13) + "…" : disp;
    ImVec2 ts = ImGui::CalcTextSize(short_d.c_str());
    dl->AddText(ImVec2(x + (sw2 - ts.x) * 0.5f, y + (sh - ts.y) * 0.5f), tcol, short_d.c_str());
  }
  for(auto &e : d.edges)
  {
    auto ai = scenters.find(e.from);
    auto bi = scenters.find(e.to);
    if(ai == scenters.end() || bi == scenters.end()) continue;
    dl->AddLine(ai->second, bi->second, lcol, 1.5f);
    float dx = bi->second.x - ai->second.x;
    float dy = bi->second.y - ai->second.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len > 1)
    {
      dx /= len;
      dy /= len;
    }
    arch_draw_arrow_head(dl, bi->second, ImVec2(dx, dy), 7.0f, lcol);
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
