#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace staterender
{
static ImVec2 state_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static std::pair<int, int> state_grid_pos(int i, int cols)
{
  return {i % cols, i / cols};
}

static ImVec2 state_rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  float dx = other.x - cen.x;
  float dy = other.y - cen.y;
  if(std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) return cen;
  float tx = hw / std::abs(dx);
  float ty = hh / std::abs(dy);
  float t = std::min(tx, ty);
  return ImVec2(cen.x + dx * t, cen.y + dy * t);
}

static ImVec2 state_circ_edge(ImVec2 cen, float r, ImVec2 other)
{
  float dx = other.x - cen.x;
  float dy = other.y - cen.y;
  float len = std::sqrt(dx * dx + dy * dy);
  if(len < 0.001f) return cen;
  return ImVec2(cen.x + dx * r / len, cen.y + dy * r / len);
}

static void state_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col)
{
  dl->AddTriangleFilled(ImVec2(tip.x, tip.y),
                        ImVec2(tip.x - dir.x * sz - dir.y * (sz * 0.5f), tip.y - dir.y * sz + dir.x * (sz * 0.5f)),
                        ImVec2(tip.x - dir.x * sz + dir.y * (sz * 0.5f), tip.y - dir.y * sz - dir.x * (sz * 0.5f)),
                        col);
}
} // namespace staterender

void render_state(const StateDiagram &d, int id)
{
  using namespace staterender;
  if(d.states.empty()) return;
  ImGui::PushID(id);
  const float sw2 = 110.0f;
  const float sh = 30.0f;
  const float hgap = 60.0f;
  const float vgap = 20.0f;
  int n = static_cast<int>(d.states.size());
  int cols = std::min(3, n);
  int rows = (n + cols - 1) / cols;
  float cw = cols * (sw2 + hgap) + hgap;
  float ch = rows * (sh + vgap) + vgap + 40;
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##st", state_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  std::vector<ImVec2> centers(n);
  for(int i = 0; i < n; ++i)
  {
    auto [col, row] = state_grid_pos(i, cols);
    float x = orig.x + hgap + col * (sw2 + hgap);
    float y = orig.y + 40 + row * (sh + vgap);
    centers[i] = ImVec2(x + sw2 * 0.5f, y + sh * 0.5f);
    auto &s = d.states[i];
    if(s.is_start)
    {
      dl->AddCircleFilled(centers[i], 10, tcol);
    }
    else if(s.is_end)
    {
      dl->AddCircleFilled(centers[i], 10, tcol);
      dl->AddCircle(centers[i], 14, tcol, 0, 2);
    }
    else
    {
      dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sw2, y + sh), fill, 12);
      dl->AddRect(ImVec2(x, y), ImVec2(x + sw2, y + sh), bord, 12);
      ImVec2 ts = ImGui::CalcTextSize(s.label.c_str());
      dl->AddText(ImVec2(x + (sw2 - ts.x) * 0.5f, y + (sh - ts.y) * 0.5f), tcol, s.label.c_str());
    }
  }
  auto find_idx = [&](const std::string &sid) -> int {
    for(int i = 0; i < n; ++i)
      if(d.states[i].id == sid) return i;
    return -1;
  };
  auto state_edge = [&](int i, ImVec2 other) -> ImVec2 {
    if(d.states[i].is_start || d.states[i].is_end) return state_circ_edge(centers[i], 10.0f, other);
    return state_rect_edge(centers[i], sw2 * 0.5f, sh * 0.5f, other);
  };
  for(auto &t : d.transitions)
  {
    int fi = find_idx(t.from);
    int ti = find_idx(t.to);
    if(fi < 0 || ti < 0) continue;
    ImVec2 a = state_edge(fi, centers[ti]);
    ImVec2 b = state_edge(ti, centers[fi]);
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len > 1)
    {
      dx /= len;
      dy /= len;
    }
    dl->AddLine(a, b, lcol, 1.5f);
    state_draw_arrow_head(dl, b, ImVec2(dx, dy), 8, lcol);
    if(!t.label.empty())
    {
      ImVec2 ts2 = ImGui::CalcTextSize(t.label.c_str());
      dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts2.x * 0.5f, (a.y + b.y) * 0.5f - ts2.y - 2), tcol, t.label.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams