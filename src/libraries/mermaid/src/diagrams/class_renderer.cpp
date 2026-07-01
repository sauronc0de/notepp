#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace classrender
{
static ImVec2 class_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static std::pair<int, int> class_grid_pos(int i, int cols)
{
  return {i % cols, i / cols};
}

static ImVec2 class_rect_edge(ImVec2 cen, float hw, float hh, ImVec2 other)
{
  float dx = other.x - cen.x;
  float dy = other.y - cen.y;
  if(std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) return cen;
  float tx = hw / std::abs(dx);
  float ty = hh / std::abs(dy);
  float t = std::min(tx, ty);
  return ImVec2(cen.x + dx * t, cen.y + dy * t);
}

static void class_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float sz, ImU32 col, bool open)
{
  if(!open)
    dl->AddTriangleFilled(ImVec2(tip.x, tip.y),
                          ImVec2(tip.x - dir.x * sz - dir.y * (sz * 0.5f), tip.y - dir.y * sz + dir.x * (sz * 0.5f)),
                          ImVec2(tip.x - dir.x * sz + dir.y * (sz * 0.5f), tip.y - dir.y * sz - dir.x * (sz * 0.5f)),
                          col);
}
} // namespace classrender

void render_class(const ClassDiagram &d, int id)
{
  using namespace classrender;
  if(d.classes.empty()) return;
  ImGui::PushID(id);
  const float cw = 150.0f;
  const float header_h = 24.0f;
  const float row_h = 18.0f;
  const float gap = 40.0f;
  int nc = static_cast<int>(d.classes.size());
  int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nc)))));
  int rows = (nc + cols - 1) / cols;
  float canvas_w = cols * (cw + gap) + gap;
  float canvas_h = 0;
  std::vector<float> heights(nc);
  for(int i = 0; i < nc; ++i)
  {
    heights[i] = header_h;
    auto &c = d.classes[i];
    if(!c.stereotype.empty()) heights[i] += 16;
    heights[i] += c.members.size() * row_h + 4;
    canvas_h = std::max(canvas_h, heights[i]);
  }
  canvas_h = rows * (canvas_h + gap) + gap;

  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##cls", class_nonzero_invisible_button_size(canvas_w, canvas_h));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hfill = ImGui::GetColorU32(ImGuiCol_TitleBg);
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);

  std::vector<ImVec2> centers(nc);
  std::vector<float> half_h(nc);
  for(int i = 0; i < nc; ++i)
  {
    auto [col, row] = class_grid_pos(i, cols);
    float x = orig.x + gap + col * (cw + gap);
    float y = orig.y + gap + row * (heights[i] + gap);
    float h = heights[i];
    centers[i] = ImVec2(x + cw * 0.5f, y + h * 0.5f);
    half_h[i] = h * 0.5f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + h), fill, 4);
    dl->AddRect(ImVec2(x, y), ImVec2(x + cw, y + h), bord, 4);
    auto &c = d.classes[i];
    float hy = y;
    if(!c.stereotype.empty())
    {
      ImVec2 sts = ImGui::CalcTextSize(c.stereotype.c_str());
      dl->AddText(ImVec2(x + (cw - sts.x) * 0.5f, hy + 2),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), c.stereotype.c_str());
      hy += 16;
    }
    dl->AddRectFilled(ImVec2(x, hy), ImVec2(x + cw, hy + header_h), hfill, 0);
    ImVec2 ns = ImGui::CalcTextSize(c.name.c_str());
    dl->AddText(ImVec2(x + (cw - ns.x) * 0.5f, hy + (header_h - ns.y) * 0.5f), tcol, c.name.c_str());
    hy += header_h;
    dl->AddLine(ImVec2(x, hy), ImVec2(x + cw, hy), bord);
    for(auto &m : c.members)
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%c %s", m.vis, m.name.c_str());
      dl->AddText(ImVec2(x + 4, hy + 2), tcol, buf);
      hy += row_h;
    }
  }
  for(auto &r : d.relations)
  {
    int fi = -1, ti = -1;
    for(int i = 0; i < nc; ++i)
    {
      if(d.classes[i].name == r.from) fi = i;
      if(d.classes[i].name == r.to) ti = i;
    }
    if(fi < 0 || ti < 0) continue;
    ImVec2 a = class_rect_edge(centers[fi], cw * 0.5f, half_h[fi], centers[ti]);
    ImVec2 b = class_rect_edge(centers[ti], cw * 0.5f, half_h[ti], centers[fi]);
    ImU32 lc2 = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddLine(a, b, lc2, 1.5f);
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if(len > 1)
    {
      dx /= len;
      dy /= len;
    }
    class_draw_arrow_head(dl, b, ImVec2(dx, dy), 8, lc2, r.type == ClassRel::T::Dependency);
    if(!r.label.empty())
    {
      ImVec2 mp((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
      ImVec2 ts = ImGui::CalcTextSize(r.label.c_str());
      dl->AddText(ImVec2(mp.x - ts.x * 0.5f, mp.y - ts.y - 2), tcol, r.label.c_str());
    }
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams