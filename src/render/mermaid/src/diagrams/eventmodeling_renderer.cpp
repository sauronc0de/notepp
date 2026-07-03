// ── eventmodeling_renderer.cpp ─────────────────────────────────────────────
//
// Event modeling diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace eventmodelingrender
{
static ImVec2 em_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static void em_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open = false)
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
} // namespace eventmodelingrender

void render_eventmodeling(const EventModelingDiagram &d, int id)
{
  using namespace eventmodelingrender;
  ImGui::PushID(id);
  const float iw = 120.0f, ih = 40.0f, hgap = 14.0f, pad = 12.0f;
  int n = static_cast<int>(d.items.size());
  if(n == 0)
  {
    ImGui::Text("(empty event model)");
    ImGui::PopID();
    return;
  }
  float cw = static_cast<float>(n) * (iw + hgap) + hgap + pad * 2.0f;
  float ch = ih + pad * 2.0f + 20.0f;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##em", em_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y), tcol, d.title.c_str());
  }
  static const float hues[] = {0.6f, 0.1f, 0.35f, 0.75f, 0.5f};
  std::unordered_map<std::string, int> item_idx;
  for(int i = 0; i < n; ++i)
  {
    float x = orig.x + pad + i * (iw + hgap);
    float y = orig.y + 20.0f + pad;
    int ti = static_cast<int>(d.items[i].type);
    float rr, gg, bb;
    ImGui::ColorConvertHSVtoRGB(hues[ti], 0.6f, 0.88f, rr, gg, bb);
    ImU32 fc = ImGui::GetColorU32(ImVec4(rr, gg, bb, 0.75f));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + iw, y + ih), fc, 5.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + iw, y + ih), bord, 5.0f);
    std::string lbl = d.items[i].name.size() > 14 ? d.items[i].name.substr(0, 13) + "…" : d.items[i].name;
    ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(x + (iw - ts.x) * 0.5f, y + (ih - ts.y) * 0.5f), tcol, lbl.c_str());
    item_idx[d.items[i].name] = i;
    if(i < n - 1)
    {
      ImVec2 a(x + iw, y + ih * 0.5f);
      ImVec2 b(x + iw + hgap, y + ih * 0.5f);
      dl->AddLine(a, b, ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.5f);
      em_draw_arrow_head(dl, b, ImVec2(1, 0), 7.0f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
