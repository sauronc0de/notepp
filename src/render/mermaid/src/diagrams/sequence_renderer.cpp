#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace seqrender
{
static ImVec2 seq_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static void seq_draw_box(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill, ImU32 bord, const std::string &label)
{
  dl->AddRectFilled(a, b, fill, 4.0f);
  dl->AddRect(a, b, bord, 4.0f, 0, 1.5f);
  ImVec2 ts = ImGui::CalcTextSize(label.c_str());
  dl->AddText(ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f),
              ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
}

static void seq_draw_arrow_head(ImDrawList *dl, ImVec2 tip, ImVec2 dir, float len, ImU32 col, bool filled)
{
  if(filled)
    dl->AddTriangleFilled(ImVec2(tip.x, tip.y),
                          ImVec2(tip.x - dir.x * len - dir.y * (len * 0.5f), tip.y - dir.y * len + dir.x * (len * 0.5f)),
                          ImVec2(tip.x - dir.x * len + dir.y * (len * 0.5f), tip.y - dir.y * len - dir.x * (len * 0.5f)),
                          col);
  else
    dl->AddLine(ImVec2(tip.x, tip.y),
                ImVec2(tip.x - dir.x * len - dir.y * (len * 0.5f), tip.y - dir.y * len + dir.x * (len * 0.5f)),
                col, 1.5f);
  dl->AddLine(ImVec2(tip.x, tip.y),
              ImVec2(tip.x - dir.x * len + dir.y * (len * 0.5f), tip.y - dir.y * len - dir.x * (len * 0.5f)),
              col, 1.5f);
}
} // namespace seqrender

void render_sequence(const SequenceDiagram &d, int id)
{
  using namespace seqrender;
  if(d.participants.empty()) return;
  ImGui::PushID(id);
  const float pw = 110.0f, ph = 28.0f;
  const float hgap = 24.0f;
  const float row_h = 28.0f;
  const int np = static_cast<int>(d.participants.size());
  int msg_count = 0;
  for(auto &e : d.events)
    if(e.type == SequenceDiagram::Event::T::Message) ++msg_count;

  float cw = np * (pw + hgap) + hgap;
  float ch = ph + (msg_count + 2) * row_h + 16.0f + ph;

  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##seq", seq_nonzero_invisible_button_size(cw, ch));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

  std::vector<float> cx(np);
  for(int i = 0; i < np; ++i)
  {
    float x = orig.x + hgap + i * (pw + hgap);
    cx[i] = x + pw * 0.5f;
    seq_draw_box(dl, ImVec2(x, orig.y), ImVec2(pw, ph), fill, bord, d.participants[i].label);
    seq_draw_box(dl, ImVec2(x, orig.y + ch - ph), ImVec2(pw, ph), fill, bord, d.participants[i].label);
    dl->AddLine(ImVec2(cx[i], orig.y + ph), ImVec2(cx[i], orig.y + ch - ph), lcol, 1.0f);
  }

  auto part_idx = [&](const std::string &id_s) {
    for(int i = 0; i < np; ++i)
      if(d.participants[i].id == id_s) return i;
    return 0;
  };

  float y = orig.y + ph + 8.0f;
  int active_depth[32] = {};
  for(auto &e : d.events)
  {
    if(e.type == SequenceDiagram::Event::T::Message)
    {
      auto &m = d.messages[e.idx];
      int fi = part_idx(m.from);
      int ti = part_idx(m.to);
      float x0 = cx[fi];
      float x1 = cx[ti];
      float cy2 = y + row_h * 0.5f;
      if(active_depth[fi] > 0)
      {
        float ax = cx[fi] - 4;
        dl->AddRectFilled(ImVec2(ax, y), ImVec2(ax + 8, y + row_h),
                          ImGui::GetColorU32(ImGuiCol_Button), 0);
      }
      if(active_depth[ti] > 0)
      {
        float ax = cx[ti] - 4;
        dl->AddRectFilled(ImVec2(ax, y), ImVec2(ax + 8, y + row_h),
                          ImGui::GetColorU32(ImGuiCol_Button), 0);
      }
      ImU32 ac = m.dotted ? lcol : tcol;
      if(m.dotted)
      {
        float dx = x1 - x0;
        float len = std::abs(dx);
        float seg = 6.0f;
        int n2 = static_cast<int>(len / seg);
        for(int k = 0; k < n2; k += 2)
        {
          float t0 = k * (dx / n2);
          float t1 = (k + 1) * (dx / n2);
          dl->AddLine(ImVec2(x0 + t0, cy2), ImVec2(x0 + t1, cy2), lcol, 1.5f);
        }
      }
      else
      {
        dl->AddLine(ImVec2(x0, cy2), ImVec2(x1, cy2), tcol, 1.5f);
      }
      float dir = (x1 > x0) ? 1.0f : -1.0f;
      seq_draw_arrow_head(dl, ImVec2(x1, cy2), ImVec2(dir, 0), 8.0f, ac, m.open);
      if(!m.text.empty())
      {
        ImVec2 ts = ImGui::CalcTextSize(m.text.c_str());
        float tx = std::min(x0, x1) + (std::abs(x1 - x0) - ts.x) * 0.5f;
        dl->AddText(ImVec2(tx, cy2 - ts.y - 2), tcol, m.text.c_str());
      }
      y += row_h;
    }
    else if(e.type == SequenceDiagram::Event::T::Note)
    {
      auto &n2 = d.notes[e.idx];
      int ni = part_idx(n2.over1);
      float nx = cx[ni] - pw * 0.5f;
      float nw = pw;
      if(!n2.over2.empty())
      {
        int ni2 = part_idx(n2.over2);
        float rx = cx[ni2] + pw * 0.5f;
        nw = rx - nx;
      }
      float ncy = y;
      dl->AddRectFilled(ImVec2(nx, ncy), ImVec2(nx + nw, ncy + row_h),
                        ImGui::GetColorU32(ImVec4(1, 1, 0.6f, 0.25f)), 3);
      dl->AddRect(ImVec2(nx, ncy), ImVec2(nx + nw, ncy + row_h), bord, 3);
      ImVec2 ts = ImGui::CalcTextSize(n2.text.c_str());
      dl->AddText(ImVec2(nx + (nw - ts.x) * 0.5f, ncy + (row_h - ts.y) * 0.5f), tcol, n2.text.c_str());
      y += row_h;
    }
    else if(e.type == SequenceDiagram::Event::T::Activate)
    {
      int pi = part_idx(e.actor_id);
      if(pi < 32) active_depth[pi]++;
    }
    else if(e.type == SequenceDiagram::Event::T::Deactivate)
    {
      int pi = part_idx(e.actor_id);
      if(pi < 32 && active_depth[pi] > 0) active_depth[pi]--;
    }
    else if(e.type == SequenceDiagram::Event::T::GroupStart)
    {
      dl->AddRectFilled(ImVec2(orig.x, y), ImVec2(orig.x + cw, y + 20),
                        ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.9f, 0.18f)));
      y += 24;
    }
    else if(e.type == SequenceDiagram::Event::T::GroupEnd)
    {
      y += 8;
    }
  }
  ImGui::PopID();
}

void render_zenuml(const SequenceDiagram &d, int id) { render_sequence(d, id); }
} // namespace MermaidDiagrams