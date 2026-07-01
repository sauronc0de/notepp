// ── sankey_renderer.cpp ────────────────────────────────────────────────────
//
// Sankey diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace sankeyrender
{
static ImVec2 sk_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 sk_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace sankeyrender

void render_sankey(const SankeyDiagram &d, int id)
{
  using namespace sankeyrender;
  ImGui::PushID(id);
  std::vector<std::string> nodes;
  auto ensure_node = [&](const std::string &n) {
    if(std::find(nodes.begin(), nodes.end(), n) == nodes.end()) nodes.push_back(n);
  };
  for(auto &f : d.flows)
  {
    ensure_node(f.source);
    ensure_node(f.target);
  }
  int nn = static_cast<int>(nodes.size());
  if(nn == 0)
  {
    ImGui::Text("(empty sankey)");
    ImGui::PopID();
    return;
  }
  std::vector<float> out_total(nn, 0.0f), in_total(nn, 0.0f);
  auto ni = [&](const std::string &n) -> int {
    for(int i = 0; i < nn; ++i)
      if(nodes[i] == n) return i;
    return 0;
  };
  for(auto &f : d.flows)
  {
    out_total[ni(f.source)] += f.value;
    in_total[ni(f.target)] += f.value;
  }
  std::vector<int> col(nn, 0);
  bool changed = true;
  while(changed)
  {
    changed = false;
    for(auto &f : d.flows)
    {
      int fi = ni(f.source);
      int ti = ni(f.target);
      if(col[ti] <= col[fi])
      {
        col[ti] = col[fi] + 1;
        changed = true;
      }
    }
  }
  int ncols = 0;
  for(int c : col) ncols = std::max(ncols, c + 1);

  const float nw = 80.0f, nh_base = 20.0f, col_gap = 100.0f, pad = 20.0f, max_nh = 140.0f;
  float max_val = 1.0f;
  for(float v : in_total) max_val = std::max(max_val, v);
  for(float v : out_total) max_val = std::max(max_val, v);
  float cw = static_cast<float>(ncols) * (nw + col_gap) + pad * 2.0f;
  float ch = nn * 60.0f + pad * 2.0f;
  ch = std::min(ch, 400.0f);
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##sk", sk_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);

  std::vector<int> col_idx(ncols, 0);
  std::vector<float> col_heights(ncols, 0.0f);
  std::vector<ImVec2> node_tl(nn), node_sz(nn);
  std::vector<int> order(nn);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return col[a] < col[b]; });
  std::vector<float> col_y(ncols, 0.0f);
  for(int ci : order)
  {
    float total = std::max(in_total[ci], out_total[ci]);
    float nh = std::max(nh_base, std::min(max_nh, total / max_val * max_nh));
    float x = orig.x + pad + col[ci] * (nw + col_gap);
    float y = orig.y + pad + col_y[col[ci]];
    node_tl[ci] = ImVec2(x, y);
    node_sz[ci] = ImVec2(nw, nh);
    col_y[col[ci]] += nh + 8.0f;
  }
  for(auto &f : d.flows)
  {
    int fi = ni(f.source);
    int ti = ni(f.target);
    float frac_s = out_total[fi] > 0 ? f.value / max_val : 0.0f;
    (void)frac_s;
    float fw = std::max(1.5f, frac_s * 20.0f);
    ImVec2 a(node_tl[fi].x + node_sz[fi].x, node_tl[fi].y + node_sz[fi].y * 0.5f);
    ImVec2 b(node_tl[ti].x, node_tl[ti].y + node_sz[ti].y * 0.5f);
    ImU32 fc = sk_series_color(fi, 0.4f);
    dl->AddBezierCubic(a, ImVec2((a.x + b.x) * 0.5f, a.y), ImVec2((a.x + b.x) * 0.5f, b.y), b, fc, fw);
  }
  for(int i = 0; i < nn; ++i)
  {
    ImU32 fc = sk_series_color(i, 0.8f);
    dl->AddRectFilled(node_tl[i], ImVec2(node_tl[i].x + node_sz[i].x, node_tl[i].y + node_sz[i].y), fc, 3.0f);
    dl->AddRect(node_tl[i], ImVec2(node_tl[i].x + node_sz[i].x, node_tl[i].y + node_sz[i].y), bord, 3.0f);
    ImVec2 ts = ImGui::CalcTextSize(nodes[i].c_str());
    dl->AddText(ImVec2(node_tl[i].x + (node_sz[i].x - ts.x) * 0.5f, node_tl[i].y + (node_sz[i].y - ts.y) * 0.5f),
                tcol, nodes[i].c_str());
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
