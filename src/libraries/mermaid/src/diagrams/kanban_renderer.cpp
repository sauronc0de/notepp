// ── kanban_renderer.cpp ─────────────────────────────────────────────────────
//
// Kanban diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace kanbanrender
{
static ImVec2 kb_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 kb_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}

struct KanbanEditState
{
  bool drag_active = false;
  int  drag_ci     = -1;
  int  drag_ri     = -1;
  std::vector<KanbanCol> work_cols;
  int  drop_ci     = -1;
  int  drop_ri     = -1;
  int  ctx_ci      = -1;
  int  ctx_ri      = -1;
  char edit_label[256] = {};
  char edit_desc[512]  = {};
  bool edit_focus  = false;
  int  add_ci      = -1;
  char add_label[256] = {};
  char add_desc[512]  = {};
  bool add_focus   = false;
};
static std::unordered_map<int, KanbanEditState> s_kb_states;

static std::string next_card_id(const KanbanDiagram &d)
{
  int mx = 0;
  for(auto &col : d.columns)
    for(auto &card : col.cards)
    {
      const auto &s = card.id;
      int i = static_cast<int>(s.size());
      while(i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1]))) --i;
      if(i < static_cast<int>(s.size())) mx = std::max(mx, std::atoi(s.c_str() + i));
    }
  return "c" + std::to_string(mx + 1);
}

static std::string serialize_kanban(const KanbanDiagram &d)
{
  std::ostringstream s;
  s << "kanban\n";
  for(auto &col : d.columns)
  {
    s << "  " << col.id << "[" << col.label << "]\n";
    for(auto &card : col.cards)
    {
      s << "    " << card.id << "[" << card.label << "]";
      if(!card.description.empty()) s << ": " << card.description;
      s << "\n";
    }
  }
  return s.str();
}

static float kanban_column_width(int column_count, float available_width,
                                 float min_width, float max_width,
                                 float hgap, float pad)
{
  if(column_count <= 0) return min_width;
  const float gap_width = std::max(0, column_count - 1) * hgap;
  const float fit_width = (available_width - pad * 2.0f - gap_width) /
                          static_cast<float>(column_count);
  if(!std::isfinite(fit_width)) return min_width;
  return std::max(min_width, std::min(fit_width, max_width));
}

static std::size_t utf8_codepoint_length(unsigned char lead)
{
  if((lead & 0x80) == 0) return 1;
  if((lead & 0xE0) == 0xC0) return 2;
  if((lead & 0xF0) == 0xE0) return 3;
  if((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

static std::string ellipsize_to_width(const std::string &text, float max_width)
{
  if(text.empty() || ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;
  static constexpr const char *kEllipsis = "...";
  const float ellipsis_width = ImGui::CalcTextSize(kEllipsis).x;
  if(max_width <= ellipsis_width) return kEllipsis;
  std::string out;
  out.reserve(text.size());
  for(std::size_t pos = 0; pos < text.size();)
  {
    std::size_t len = utf8_codepoint_length(static_cast<unsigned char>(text[pos]));
    if(pos + len > text.size()) len = 1;
    std::string next = out;
    next.append(text, pos, len);
    next += kEllipsis;
    if(ImGui::CalcTextSize(next.c_str()).x > max_width) break;
    out.append(text, pos, len);
    pos += len;
  }
  out += kEllipsis;
  return out;
}

} // namespace kanbanrender

void render_kanban(const KanbanDiagram &d, int id)
{
  using namespace kanbanrender;
  ImGui::PushID(id);
  auto &es = s_kb_states[id];

  const float card_h = 32.0f, col_header_h = 28.0f, hgap = 10.0f, vgap = 6.0f, pad = 10.0f;
  const float min_col_w = 120.0f, max_col_w = 240.0f;

  const std::vector<KanbanCol> &cols = es.drag_active ? es.work_cols : d.columns;
  int nc = static_cast<int>(cols.size());
  if(nc <= 0) { ImGui::PopID(); return; }
  int max_cards = 0;
  for(auto &c : cols) max_cards = std::max(max_cards, static_cast<int>(c.cards.size()));
  float col_w = kanban_column_width(nc, ImGui::GetContentRegionAvail().x,
                                    min_col_w, max_col_w, hgap, pad);
  float cw = nc * col_w + std::max(0, nc - 1) * hgap + pad * 2.0f;
  float col_body_h = vgap + (max_cards + 1) * (card_h + vgap);
  float ch = col_header_h + col_body_h + pad * 2.0f;

  const ImVec2 orig = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##kb", kb_nonzero_invisible_button_size(cw, ch));
  const bool kb_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  const bool kb_active = ImGui::IsItemActive();
  ImDrawList *dl = ImGui::GetWindowDrawList();

  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hcol = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  const ImVec2 mouse = ImGui::GetIO().MousePos;

  struct CardRect { int ci, ri; float x, y; };
  std::vector<CardRect> crects;
  for(int i = 0; i < nc; ++i)
  {
    float x = orig.x + pad + i * (col_w + hgap);
    float cy = orig.y + pad + col_header_h + vgap;
    for(int j = 0; j < static_cast<int>(cols[i].cards.size()); ++j)
    {
      crects.push_back({i, j, x + 4, cy});
      cy += card_h + vgap;
    }
  }

  int hov_ci = -1, hov_ri = -1;
  if(kb_hovered || es.drag_active)
  {
    for(auto &r : crects)
      if(mouse.x >= r.x && mouse.x < r.x + (col_w - 8) && mouse.y >= r.y && mouse.y < r.y + card_h)
      { hov_ci = r.ci; hov_ri = r.ri; break; }
  }

  if(!es.drag_active && kb_active && ImGui::IsMouseDragging(0, 5.0f))
  {
    ImVec2 dp = ImGui::GetMouseDragDelta(0);
    ImVec2 pp = {mouse.x - dp.x, mouse.y - dp.y};
    for(auto &r : crects)
      if(pp.x >= r.x && pp.x < r.x + (col_w - 8) && pp.y >= r.y && pp.y < r.y + card_h)
      {
        es.drag_active = true; es.drag_ci = r.ci; es.drag_ri = r.ri;
        es.work_cols = d.columns;
        es.drop_ci = r.ci; es.drop_ri = r.ri;
        break;
      }
  }

  if(es.drag_active)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    for(int i = 0; i < nc; ++i)
    {
      float cx = orig.x + pad + i * (col_w + hgap);
      if(mouse.x >= cx && mouse.x < cx + col_w)
      {
        es.drop_ci = i;
        float body_top = orig.y + pad + col_header_h + vgap;
        int nj = static_cast<int>(cols[i].cards.size());
        es.drop_ri = nj;
        for(int j = 0; j < nj; ++j)
        {
          if(mouse.y < body_top + j * (card_h + vgap) + card_h * 0.5f) { es.drop_ri = j; break; }
        }
        break;
      }
    }
    if(!ImGui::IsMouseDown(0))
    {
      if(es.drop_ci >= 0)
      {
        KanbanDiagram nd = d;
        KanbanCard moved = nd.columns[es.drag_ci].cards[es.drag_ri];
        nd.columns[es.drag_ci].cards.erase(nd.columns[es.drag_ci].cards.begin() + es.drag_ri);
        int ins = es.drop_ri;
        if(es.drop_ci == es.drag_ci && es.drop_ri > es.drag_ri) --ins;
        ins = std::max(0, std::min(ins, static_cast<int>(nd.columns[es.drop_ci].cards.size())));
        nd.columns[es.drop_ci].cards.insert(nd.columns[es.drop_ci].cards.begin() + ins, moved);
        g_pending_edit = {id, serialize_kanban(nd)};
      }
      es.drag_active = false; es.drag_ci = -1; es.drag_ri = -1;
      es.drop_ci = -1; es.drop_ri = -1; es.work_cols.clear();
    }
  }
  else if(hov_ci >= 0)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  bool open_col_ctx = false;
  if(kb_hovered && !es.drag_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
  {
    g_consumed_right_click = true;
    if(hov_ci >= 0 && hov_ri >= 0)
    {
      es.ctx_ci = hov_ci; es.ctx_ri = hov_ri;
      const auto &card = cols[hov_ci].cards[hov_ri];
      std::strncpy(es.edit_label, card.label.c_str(), 255); es.edit_label[255] = '\0';
      std::strncpy(es.edit_desc, card.description.c_str(), 511); es.edit_desc[511] = '\0';
      es.edit_focus = true;
      ImGui::OpenPopup("##kb_edit");
    }
    else
    {
      for(int i = 0; i < nc; ++i)
      {
        float cx = orig.x + pad + i * (col_w + hgap);
        if(mouse.x >= cx && mouse.x < cx + col_w)
        { es.add_ci = i; open_col_ctx = true; break; }
      }
    }
  }
  if(open_col_ctx) ImGui::OpenPopup("##kb_col_ctx");

  for(int i = 0; i < nc; ++i)
  {
    float x = orig.x + pad + i * (col_w + hgap);
    float y = orig.y + pad;
    float body_top = y + col_header_h;
    ImU32 hc = kb_series_color(i, 0.6f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + col_w, y + col_header_h), hc, 4.0f);
    std::string col_label = ellipsize_to_width(cols[i].label, col_w - 12.0f);
    ImVec2 ls = ImGui::CalcTextSize(col_label.c_str());
    dl->AddText(ImVec2(x + (col_w - ls.x) * 0.5f, y + (col_header_h - ls.y) * 0.5f), tcol, col_label.c_str());
    dl->AddRectFilled(ImVec2(x, body_top), ImVec2(x + col_w, y + ch - pad * 2),
                      kb_series_color(i, 0.08f), 0.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + col_w, y + ch - pad * 2), bord, 4.0f);

    float cy = body_top + vgap;
    int nj = static_cast<int>(cols[i].cards.size());
    for(int j = 0; j < nj; ++j)
    {
      if(es.drag_active && es.drop_ci == i && es.drop_ri == j)
        dl->AddRectFilled(ImVec2(x + 4, cy - vgap * 0.5f - 1),
                          ImVec2(x + col_w - 4, cy - vgap * 0.5f + 1),
                          kb_series_color(i, 0.9f), 2.0f);
      bool is_dragging = (es.drag_active && i == es.drag_ci && j == es.drag_ri);
      bool is_hovered = (hov_ci == i && hov_ri == j && !es.drag_active);
      if(is_dragging)
      {
        dl->AddRectFilled(ImVec2(x + 4, cy), ImVec2(x + col_w - 4, cy + card_h),
                          ImGui::ColorConvertFloat4ToU32({0.5f, 0.5f, 0.5f, 0.15f}), 3.0f);
        dl->AddRect(ImVec2(x + 4, cy), ImVec2(x + col_w - 4, cy + card_h), bord, 3.0f, 0, 1.0f);
      }
      else
      {
        dl->AddRectFilled(ImVec2(x + 4, cy), ImVec2(x + col_w - 4, cy + card_h),
                          is_hovered ? hcol : fill, 3.0f);
        dl->AddRect(ImVec2(x + 4, cy), ImVec2(x + col_w - 4, cy + card_h),
                    is_hovered ? kb_series_color(i, 0.8f) : bord, 3.0f);
        const auto &card = cols[i].cards[j];
        std::string lbl = ellipsize_to_width(card.label, col_w - 20.0f);
        ImVec2 cs = ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(x + 4 + (col_w - 8 - cs.x) * 0.5f, cy + (card_h - cs.y) * 0.5f), tcol, lbl.c_str());
        if(!card.description.empty())
          dl->AddCircleFilled(ImVec2(x + col_w - 10, cy + card_h - 8), 3.0f, kb_series_color(i, 0.7f));
      }
      cy += card_h + vgap;
    }
    if(es.drag_active && es.drop_ci == i && es.drop_ri >= nj)
      dl->AddRectFilled(ImVec2(x + 4, cy - vgap * 0.5f - 1),
                        ImVec2(x + col_w - 4, cy - vgap * 0.5f + 1),
                        kb_series_color(i, 0.9f), 2.0f);
  }

  if(es.drag_active && es.drag_ci >= 0 && es.drag_ci < static_cast<int>(d.columns.size())
     && es.drag_ri >= 0 && es.drag_ri < static_cast<int>(d.columns[es.drag_ci].cards.size()))
  {
    const auto &dc = d.columns[es.drag_ci].cards[es.drag_ri];
    float fx = mouse.x - col_w * 0.5f;
    float fy = mouse.y - card_h * 0.5f;
    dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + col_w - 8, fy + card_h),
                      ImGui::ColorConvertFloat4ToU32({0.2f, 0.2f, 0.2f, 0.85f}), 3.0f);
    dl->AddRect(ImVec2(fx, fy), ImVec2(fx + col_w - 8, fy + card_h),
                kb_series_color(es.drag_ci, 0.9f), 3.0f, 0, 1.5f);
    std::string lbl = ellipsize_to_width(dc.label, col_w - 20.0f);
    ImVec2 cs = ImGui::CalcTextSize(lbl.c_str());
    dl->AddText(ImVec2(fx + (col_w - 8 - cs.x) * 0.5f, fy + (card_h - cs.y) * 0.5f), tcol, lbl.c_str());
  }

  if(hov_ci >= 0 && hov_ri >= 0 && !es.drag_active)
  {
    const auto &card = cols[hov_ci].cards[hov_ri];
    ImGui::SetTooltip("%s", card.description.empty() ? card.label.c_str() : card.description.c_str());
  }

  bool open_add_from_ctx = false;
  if(ImGui::BeginPopup("##kb_col_ctx"))
  {
    if(ImGui::MenuItem("Add card"))
    {
      es.add_label[0] = '\0';
      es.add_desc[0] = '\0';
      es.add_focus = true;
      open_add_from_ctx = true;
    }
    ImGui::EndPopup();
  }
  if(open_add_from_ctx) ImGui::OpenPopup("##kb_add");

  if(ImGui::BeginPopup("##kb_edit"))
  {
    if(es.ctx_ci >= 0 && es.ctx_ci < static_cast<int>(d.columns.size())
       && es.ctx_ri >= 0 && es.ctx_ri < static_cast<int>(d.columns[es.ctx_ci].cards.size()))
    {
      ImGui::TextDisabled("Edit card");
      ImGui::Separator();
      ImGui::Text("Label:");
      if(es.edit_focus) { ImGui::SetKeyboardFocusHere(); es.edit_focus = false; }
      ImGui::SetNextItemWidth(220);
      ImGui::InputText("##kb_lbl", es.edit_label, 256);
      ImGui::Text("Description:");
      ImGui::SetNextItemWidth(220);
      ImGui::InputTextMultiline("##kb_desc", es.edit_desc, 512, ImVec2(220, 60));
      ImGui::Separator();
      bool confirm = ImGui::Button("OK");
      if(confirm && es.edit_label[0])
      {
        KanbanDiagram nd = d;
        nd.columns[es.ctx_ci].cards[es.ctx_ri].label = es.edit_label;
        nd.columns[es.ctx_ci].cards[es.ctx_ri].description = es.edit_desc;
        g_pending_edit = {id, serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::SameLine();
      if(ImGui::Button("Delete"))
      {
        KanbanDiagram nd = d;
        nd.columns[es.ctx_ci].cards.erase(nd.columns[es.ctx_ci].cards.begin() + es.ctx_ri);
        g_pending_edit = {id, serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("##kb_add"))
  {
    if(es.add_ci >= 0 && es.add_ci < static_cast<int>(d.columns.size()))
    {
      ImGui::TextDisabled("New card — %s", d.columns[es.add_ci].label.c_str());
      ImGui::Separator();
      ImGui::Text("Label:");
      if(es.add_focus) { ImGui::SetKeyboardFocusHere(); es.add_focus = false; }
      ImGui::SetNextItemWidth(220);
      bool enter = ImGui::InputText("##kb_add_lbl", es.add_label, 256, ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Text("Description:");
      ImGui::SetNextItemWidth(220);
      ImGui::InputTextMultiline("##kb_add_desc", es.add_desc, 512, ImVec2(220, 60));
      ImGui::Separator();
      bool confirm = ImGui::Button("Add") || enter;
      if(confirm && es.add_label[0])
      {
        KanbanDiagram nd = d;
        KanbanCard nc_card;
        nc_card.id = next_card_id(d);
        nc_card.label = es.add_label;
        nc_card.description = es.add_desc;
        nd.columns[es.add_ci].cards.push_back(nc_card);
        g_pending_edit = {id, serialize_kanban(nd)};
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::PopID();
}
} // namespace MermaidDiagrams
