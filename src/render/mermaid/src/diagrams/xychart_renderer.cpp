// ── xychart_renderer.cpp ───────────────────────────────────────────────────
//
// XY chart diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace xychartrender
{
static ImVec2 xy_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 xy_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}
} // namespace xychartrender

void render_xychart(const XYDiagram &d, int id)
{
  using namespace xychartrender;
  ImGui::PushID(id);
  const float pad = 12.0f;
  int nc = static_cast<int>(d.x_labels.size());
  if(nc == 0 && !d.series.empty()) nc = static_cast<int>(d.series[0].data.size());
  nc = std::max(nc, 1);
  const int ns = std::max(1, static_cast<int>(d.series.size()));
  const float cw = std::max(120.0f, d.config.width);
  const float ch = std::max(120.0f, d.config.height);
  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##xy", xy_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 gcol = ImGui::GetColorU32(ImGuiCol_Separator);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  float top = pad;
  if(d.config.show_title && !d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, orig.y + top), tcol, d.title.c_str());
    top += ts.y + d.config.title_padding;
  }

  float left = pad + 44.0f;
  float right = pad + 10.0f;
  float bottom = pad + 24.0f;
  if(d.config.x_axis.show_label) bottom += 20.0f + d.config.x_axis.label_padding;
  if(d.config.x_axis.show_title && !d.x_title.empty()) bottom += 20.0f + d.config.x_axis.title_padding;
  if(d.config.y_axis.show_label) left += 24.0f + d.config.y_axis.label_padding;
  if(d.config.y_axis.show_title && !d.y_title.empty())
    left += ImGui::CalcTextSize(d.y_title.c_str()).x + d.config.y_axis.title_padding;

  float plot_w = std::max(80.0f, cw - left - right);
  float plot_h = std::max(60.0f, ch - top - bottom);
  float ox = orig.x + left;
  float oy = orig.y + top;
  float range = d.y_max - d.y_min;
  if(range <= 0) range = 1.0f;

  for(int g = 0; g <= 4; ++g)
  {
    float gy = oy + plot_h * (1.0f - g * 0.25f);
    dl->AddLine(ImVec2(ox, gy), ImVec2(ox + plot_w, gy), gcol, 1.0f);
    float val = d.y_min + range * g * 0.25f;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f", val);
    if(d.config.y_axis.show_label)
    {
      ImVec2 ts = ImGui::CalcTextSize(buf);
      dl->AddText(ImVec2(ox - d.config.y_axis.tick_length - ts.x - 5.0f, gy - ts.y * 0.5f), lcol, buf);
    }
    if(d.config.y_axis.show_tick)
      dl->AddLine(ImVec2(ox - d.config.y_axis.tick_length, gy), ImVec2(ox, gy),
                  tcol, d.config.y_axis.tick_width);
  }

  if(d.config.y_axis.show_axis_line)
    dl->AddLine(ImVec2(ox, oy), ImVec2(ox, oy + plot_h), tcol, d.config.y_axis.axis_line_width);
  if(d.config.x_axis.show_axis_line)
    dl->AddLine(ImVec2(ox, oy + plot_h), ImVec2(ox + plot_w, oy + plot_h),
                tcol, d.config.x_axis.axis_line_width);

  const float slot = d.horizontal ? plot_h / static_cast<float>(nc) : plot_w / static_cast<float>(nc);
  for(int xi = 0; xi < nc; ++xi)
  {
    if(d.config.x_axis.show_tick)
    {
      if(d.horizontal)
      {
        const float y = oy + xi * slot + slot * 0.5f;
        dl->AddLine(ImVec2(ox - d.config.x_axis.tick_length, y), ImVec2(ox, y),
                    tcol, d.config.x_axis.tick_width);
      }
      else
      {
        const float x = ox + xi * slot + slot * 0.5f;
        dl->AddLine(ImVec2(x, oy + plot_h), ImVec2(x, oy + plot_h + d.config.x_axis.tick_length),
                    tcol, d.config.x_axis.tick_width);
      }
    }
  }

  for(int si = 0; si < static_cast<int>(d.series.size()); ++si)
  {
    const auto &s = d.series[si];
    ImU32 sc = xy_series_color(si);
    std::vector<ImVec2> line_pts;
    for(int xi = 0; xi < static_cast<int>(s.data.size()) && xi < nc; ++xi)
    {
      const float frac = (s.data[xi] - d.y_min) / range;
      char val_buf[24];
      std::snprintf(val_buf, sizeof(val_buf), "%.0f", s.data[xi]);
      if(d.horizontal)
      {
        const float y = oy + xi * slot;
        const float bh = std::max(1.0f, slot / static_cast<float>(ns) - 2.0f);
        const float y2 = y + si * bh + 1.0f;
        const float x2 = ox + plot_w * frac;
        if(s.is_bar)
        {
          dl->AddRectFilled(ImVec2(ox, y2), ImVec2(x2, y2 + bh),
                            xy_series_color(si, 0.7f), 2.0f);
          if(d.config.show_data_label)
          {
            ImVec2 ts = ImGui::CalcTextSize(val_buf);
            float tx = d.config.show_data_label_outside_bar ? x2 + 4.0f : std::max(ox + 2.0f, x2 - ts.x - 4.0f);
            dl->AddText(ImVec2(tx, y2 + (bh - ts.y) * 0.5f), tcol, val_buf);
          }
        }
        else
        {
          line_pts.push_back(ImVec2(x2, y + slot * 0.5f));
        }
      }
      else
      {
        const float x = ox + xi * slot;
        const float by = oy + plot_h * (1.0f - frac);
        if(s.is_bar)
        {
          const float bw = std::max(1.0f, slot / static_cast<float>(ns) - 2.0f);
          const float bx = x + si * bw + 1.0f;
          dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, oy + plot_h),
                            xy_series_color(si, 0.7f), 2.0f);
          if(d.config.show_data_label)
          {
            ImVec2 ts = ImGui::CalcTextSize(val_buf);
            float ty = d.config.show_data_label_outside_bar ? by - ts.y - 2.0f : by + 3.0f;
            dl->AddText(ImVec2(bx + (bw - ts.x) * 0.5f, ty), tcol, val_buf);
          }
        }
        else
        {
          line_pts.push_back(ImVec2(x + slot * 0.5f, by));
        }
      }
    }
    if(!line_pts.empty() && line_pts.size() > 1)
      for(int k = 0; k < static_cast<int>(line_pts.size()) - 1; ++k)
        dl->AddLine(line_pts[k], line_pts[k + 1], sc, 2.0f);
    for(auto &p : line_pts) dl->AddCircleFilled(p, 3.0f, sc);
  }

  if(d.config.x_axis.show_label)
  {
    for(int xi = 0; xi < nc && xi < static_cast<int>(d.x_labels.size()); ++xi)
    {
      std::string lbl = d.x_labels[xi];
      if(lbl.size() > 10) lbl = lbl.substr(0, 9) + "...";
      ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
      if(d.horizontal)
        dl->AddText(ImVec2(ox - ts.x - d.config.x_axis.label_padding, oy + xi * slot + (slot - ts.y) * 0.5f), lcol, lbl.c_str());
      else
        dl->AddText(ImVec2(ox + xi * slot + (slot - ts.x) * 0.5f, oy + plot_h + d.config.x_axis.tick_length + d.config.x_axis.label_padding), lcol, lbl.c_str());
    }
  }
  if(d.config.x_axis.show_title && !d.x_title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.x_title.c_str());
    dl->AddText(ImVec2(ox + (plot_w - ts.x) * 0.5f, orig.y + ch - pad - ts.y), tcol, d.x_title.c_str());
  }
  if(d.config.y_axis.show_title && !d.y_title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.y_title.c_str());
    dl->AddText(ImVec2(orig.x + pad, oy + (plot_h - ts.y) * 0.5f), tcol, d.y_title.c_str());
  }
  ImGui::PopID();
}
} // namespace MermaidDiagrams
