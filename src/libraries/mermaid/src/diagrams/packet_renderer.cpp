// ── packet_renderer.cpp ────────────────────────────────────────────────────
//
// Packet diagram renderer for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

namespace MermaidDiagrams
{
namespace packetrender
{
static ImVec2 pkt_nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static ImU32 pkt_series_color(int i, float alpha = 1.0f)
{
  float h = static_cast<float>(i) * 0.618034f;
  h -= std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, 0.65f, 0.90f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}

struct PacketEditState
{
  PacketConfig cfg_edit;
  bool         cfg_open     = false;
  int          ctx_fi       = -1;
  int          rename_fi    = -1;
  char         rename_buf[256] = {};
  bool         rename_focus = false;
  int          add_after    = -2;
  char         add_name[256] = {};
  int          add_bits     = 8;
  bool         add_focus    = false;
  bool         drag_active  = false;
  int          drag_fi      = -1;
  std::vector<PacketField> drag_fields;
  std::vector<int>         drag_colors;
  bool  resize_active    = false;
  int   resize_fi        = -1;
  int   resize_orig_bits = 0;
  float resize_start_x   = 0.0f;
};

static std::unordered_map<int, PacketEditState> s_pkt_states;

static std::string serialize_packet(const PacketDiagram &d)
{
  const PacketConfig &c = d.config;
  std::ostringstream s;
  s << "%%{init: {'packet': {"
    << "'bitWidth': "    << static_cast<int>(c.bitWidth)
    << ", 'rowHeight': " << static_cast<int>(c.rowHeight)
    << ", 'bitsPerRow': "<< c.bitsPerRow
    << ", 'showBits': "  << (c.showBits   ? "true" : "false")
    << ", 'paddingX': "  << static_cast<int>(c.paddingX)
    << ", 'paddingY': "  << static_cast<int>(c.paddingY)
    << ", 'showLegend': "<< (c.showLegend ? "true" : "false")
    << "}}}%%\n"
    << "packet-beta\n";
  if(!d.title.empty()) s << "  title \"" << d.title << "\"\n";
  int cur = 0;
  for(auto &f : d.fields)
  {
    int bits = std::max(1, f.end - f.start + 1);
    s << "  " << cur << "-" << (cur + bits - 1) << ": \"" << f.name << "\"\n";
    cur += bits;
  }
  return s.str();
}

static void rebuild_bits(std::vector<PacketField> &fields)
{
  int cur = 0;
  for(auto &f : fields)
  {
    int bits = std::max(1, f.end - f.start + 1);
    f.start = cur;
    f.end = cur + bits - 1;
    cur = f.end + 1;
  }
}

} // namespace packetrender

void render_packet(const PacketDiagram &d, int id)
{
  using namespace packetrender;
  ImGui::PushID(id);
  auto &es = s_pkt_states[id];

  const std::vector<PacketField> &wf = (es.drag_active || es.resize_active) ? es.drag_fields : d.fields;
  const int NF = static_cast<int>(wf.size());

  const PacketConfig &cfg = d.config;
  const float bit_w = cfg.bitWidth, fh = cfg.rowHeight;
  const float px = cfg.paddingX, py = cfg.paddingY;
  const float hdr_h = cfg.showBits ? 16.0f : 0.0f;
  const float sq = 10.0f, lgap = 5.0f, igap = 10.0f;
  const float outer = 4.0f;

  int wf_total = 0;
  for(auto &f : wf) wf_total = std::max(wf_total, f.end + 1);
  int total = std::max(std::max(wf_total, d.total_bits), 1);
  int bpr = std::max(1, cfg.bitsPerRow);
  int n_rows = (total + bpr - 1) / bpr;
  float cw = static_cast<float>(bpr) * bit_w + outer * 2.0f;

  std::vector<bool> ext(NF, false);
  std::vector<int>  label_row(NF, 0);
  for(int fi = 0; fi < NF; ++fi)
  {
    auto &f = wf[fi];
    int best_seg = 0, best_r = 0;
    for(int row = 0; row < n_rows; ++row)
    {
      int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if(fs <= fe) { int seg = fe - fs + 1; if(seg > best_seg) { best_seg = seg; best_r = row; } }
    }
    label_row[fi] = best_r;
    ext[fi] = ImGui::CalcTextSize(f.name.c_str()).x > static_cast<float>(best_seg) * bit_w - px - 4.0f;
  }
  bool any_ext = false;
  for(bool b : ext) if(b) { any_ext = true; break; }

  float legend_h = 0.0f;
  if(any_ext && cfg.showLegend)
  {
    float avail_w = cw - outer * 2.0f, lx_s = 0.0f;
    int lrows = 1;
    for(int fi = 0; fi < NF; ++fi)
    {
      if(!ext[fi]) continue;
      float iw = sq + lgap + ImGui::CalcTextSize(wf[fi].name.c_str()).x + igap;
      if(lx_s + iw > avail_w && lx_s > 0.0f) { lx_s = 0.0f; lrows++; }
      lx_s += iw;
    }
    legend_h = py + static_cast<float>(lrows) * 18.0f;
  }

  const float title_h = d.title.empty() ? 0.0f : (py * 0.5f + 16.0f);
  const float row_step = hdr_h + fh + py;
  const float ch = py * 0.5f + title_h + static_cast<float>(n_rows) * row_step + legend_h;

  const ImVec2 orig = ImGui::GetCursorScreenPos();

  auto vis_rect = [&](int rs, int re, int fs, int fe) -> std::pair<float, float> {
    float li = (fs == rs) ? 0.0f : px * 0.5f;
    float ri = (fe == re) ? 0.0f : px * 0.5f;
    float vfx = orig.x + outer + static_cast<float>(fs - rs) * bit_w + li;
    float vfw = static_cast<float>(fe - fs + 1) * bit_w - li - ri;
    if(vfw < 1.0f) { vfx -= li; vfw += li + ri; }
    return {vfx, vfw};
  };

  struct FR { int fi, fe; float x, y, w; };
  std::vector<FR> frects;
  frects.reserve(NF * n_rows);
  {
    const float yb0 = orig.y + py * 0.5f + title_h;
    for(int row = 0; row < n_rows; ++row)
    {
      int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
      float fy0 = yb0 + static_cast<float>(row) * row_step + hdr_h;
      for(int fi = 0; fi < NF; ++fi)
      {
        int fs = std::max(wf[fi].start, rs), fe = std::min(wf[fi].end, re);
        if(fs > fe) continue;
        auto [vfx, vfw] = vis_rect(rs, re, fs, fe);
        frects.push_back({fi, fe, vfx, fy0, vfw});
      }
    }
  }

  ImGui::InvisibleButton("##pkt", pkt_nonzero_invisible_button_size(cw, ch));
  const bool pkt_hovered = ImGui::IsItemHovered();
  const bool pkt_active = ImGui::IsItemActive();

  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float edge_thresh = 6.0f;

  int hovered_fi = -1;
  for(auto &r : frects)
    if(mouse.x >= r.x && mouse.x < r.x + r.w && mouse.y >= r.y && mouse.y < r.y + fh)
    { hovered_fi = r.fi; break; }
  if(!pkt_hovered && !es.drag_active && !es.resize_active) hovered_fi = -1;

  int edge_fi = -1;
  if(!es.drag_active && !es.resize_active)
  {
    for(auto &r : frects)
    {
      if(r.fe != wf[r.fi].end) continue;
      float right = r.x + r.w;
      if(mouse.x >= right - edge_thresh && mouse.x <= right + edge_thresh &&
         mouse.y >= r.y && mouse.y < r.y + fh)
      { edge_fi = r.fi; break; }
    }
    if(!pkt_hovered) edge_fi = -1;
  }

  if(!es.drag_active && !es.resize_active && pkt_active && ImGui::IsMouseDragging(0, 5.0f))
  {
    ImVec2 dp = ImGui::GetMouseDragDelta(0);
    ImVec2 pp = {mouse.x - dp.x, mouse.y - dp.y};
    bool started = false;
    for(auto &r : frects)
    {
      if(r.fe != wf[r.fi].end) continue;
      float right = r.x + r.w;
      if(pp.x >= right - edge_thresh && pp.x <= right + edge_thresh &&
         pp.y >= r.y && pp.y < r.y + fh)
      {
        es.resize_active = true;
        es.resize_fi = r.fi;
        es.resize_orig_bits = wf[r.fi].end - wf[r.fi].start + 1;
        es.resize_start_x = pp.x;
        es.drag_fields = d.fields;
        es.drag_colors.resize(d.fields.size());
        std::iota(es.drag_colors.begin(), es.drag_colors.end(), 0);
        started = true;
        break;
      }
    }
    if(!started)
    {
      for(auto &r : frects)
        if(pp.x >= r.x && pp.x < r.x + r.w && pp.y >= r.y && pp.y < r.y + fh)
        {
          es.drag_active = true;
          es.drag_fi = r.fi;
          es.drag_fields = d.fields;
          es.drag_colors.resize(d.fields.size());
          std::iota(es.drag_colors.begin(), es.drag_colors.end(), 0);
          break;
        }
    }
  }

  if(es.resize_active)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    int fi = es.resize_fi;
    float delta_x = mouse.x - es.resize_start_x;
    int delta_bits = static_cast<int>(std::round(delta_x / bit_w));
    int new_bits = std::max(1, es.resize_orig_bits + delta_bits);
    es.drag_fields = d.fields;
    if(fi < static_cast<int>(es.drag_fields.size()))
    {
      int new_end = es.drag_fields[fi].start + new_bits - 1;
      int diff = new_end - es.drag_fields[fi].end;
      es.drag_fields[fi].end = new_end;
      for(int k = fi + 1; k < static_cast<int>(es.drag_fields.size()); ++k)
      {
        es.drag_fields[k].start += diff;
        es.drag_fields[k].end += diff;
      }
    }
    if(!ImGui::IsMouseDown(0))
    {
      if(new_bits != es.resize_orig_bits && fi < static_cast<int>(d.fields.size()))
      {
        PacketDiagram nd = d;
        nd.fields = es.drag_fields;
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      es.resize_active = false;
      es.resize_fi = -1;
      es.drag_fields.clear();
      es.drag_colors.clear();
    }
  }

  if(es.drag_active)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if(!ImGui::IsMouseDown(0))
    {
      bool changed = false;
      for(int i = 0; i < static_cast<int>(d.fields.size()) && i < static_cast<int>(es.drag_fields.size()); ++i)
        if(d.fields[i].name != es.drag_fields[i].name ||
           (d.fields[i].end - d.fields[i].start) != (es.drag_fields[i].end - es.drag_fields[i].start))
        { changed = true; break; }
      if(changed)
      {
        PacketDiagram nd = d;
        nd.fields = es.drag_fields;
        nd.total_bits = 0;
        for(auto &f : nd.fields) nd.total_bits = std::max(nd.total_bits, f.end + 1);
        g_pending_edit = {id, serialize_packet(nd)};
      }
      es.drag_active = false;
      es.drag_fi = -1;
      es.drag_fields.clear();
      es.drag_colors.clear();
    }
    else if(hovered_fi >= 0 && hovered_fi != es.drag_fi &&
            hovered_fi < static_cast<int>(es.drag_fields.size()))
    {
      std::swap(es.drag_fields[es.drag_fi], es.drag_fields[hovered_fi]);
      if(es.drag_fi < static_cast<int>(es.drag_colors.size()) && hovered_fi < static_cast<int>(es.drag_colors.size()))
        std::swap(es.drag_colors[es.drag_fi], es.drag_colors[hovered_fi]);
      rebuild_bits(es.drag_fields);
      es.drag_fi = hovered_fi;
    }
  }
  else if(edge_fi >= 0)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  else if(hovered_fi >= 0)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  auto ci = [&](int fi) -> int {
    return (es.drag_active && fi < static_cast<int>(es.drag_colors.size())) ? es.drag_colors[fi] : fi;
  };

  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 tcol = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 lcol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 bord = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 hcol = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
  ImVec4 lbg4 = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  lbg4.w = 0.88f;
  const ImU32 lbg = ImGui::GetColorU32(lbg4);

  float y_base = orig.y + py * 0.5f;

  if(!d.title.empty())
  {
    ImVec2 ts = ImGui::CalcTextSize(d.title.c_str());
    dl->AddText(ImVec2(orig.x + (cw - ts.x) * 0.5f, y_base), tcol, d.title.c_str());
    y_base += title_h;
  }

  for(int row = 0; row < n_rows; ++row)
  {
    int rs = row * bpr, re = std::min(rs + bpr, total) - 1;
    float ry = y_base + static_cast<float>(row) * row_step;
    float fy = ry + hdr_h;

    auto vr = [&](int fs, int fe) { return vis_rect(rs, re, fs, fe); };

    if(cfg.showBits)
    {
      auto bit_hdr_x = [&](int bit) -> float {
        for(int fi2 = 0; fi2 < NF; ++fi2)
        {
          int fs2 = std::max(wf[fi2].start, rs), fe2 = std::min(wf[fi2].end, re);
          if(bit >= fs2 && bit <= fe2)
          {
            auto [vfx2, vfw2] = vr(fs2, fe2);
            return vfx2 + static_cast<float>(bit - fs2) / static_cast<float>(fe2 - fs2 + 1) * vfw2;
          }
        }
        return orig.x + outer + static_cast<float>(bit - rs) * bit_w;
      };
      for(int i = rs; i <= re; i += 8)
      {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", i);
        dl->AddText(ImVec2(bit_hdr_x(i), ry), lcol, buf);
      }
    }

    for(int fi = 0; fi < NF; ++fi)
    {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if(fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      dl->AddRectFilled(ImVec2(vfx, fy), ImVec2(vfx + vfw, fy + fh), pkt_series_color(ci(fi), 0.42f), 3.0f);
      dl->AddRect(ImVec2(vfx, fy), ImVec2(vfx + vfw, fy + fh), pkt_series_color(ci(fi), 0.80f), 3.0f, 0, 1.5f);
      if(fi == hovered_fi)
        dl->AddRect(ImVec2(vfx - 1, fy - 1), ImVec2(vfx + vfw + 1, fy + fh + 1), hcol, 4.0f, 0, 2.0f);
    }

    for(int fi = 0; fi < NF; ++fi)
    {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if(fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      int nbits = fe - fs + 1;
      for(int j = 0; j <= nbits; ++j)
      {
        float tx = vfx + static_cast<float>(j) / static_cast<float>(nbits) * vfw;
        dl->AddLine(ImVec2(tx, fy), ImVec2(tx, fy + fh), bord, 0.5f);
      }
    }

    for(int fi = 0; fi < NF; ++fi)
    {
      auto &f = wf[fi];
      int fs = std::max(f.start, rs), fe = std::min(f.end, re);
      if(fs > fe) continue;
      auto [vfx, vfw] = vr(fs, fe);
      if(!ext[fi] && row == label_row[fi])
      {
        ImVec2 ts = ImGui::CalcTextSize(f.name.c_str());
        float lx2 = vfx + (vfw - ts.x) * 0.5f;
        float ly2 = fy + (fh - ts.y) * 0.5f;
        const float lp = 3.0f;
        dl->AddRectFilled(ImVec2(lx2 - lp, ly2 - lp), ImVec2(lx2 + ts.x + lp, ly2 + ts.y + lp), lbg, 2.5f);
        dl->AddText(ImVec2(lx2, ly2), tcol, f.name.c_str());
      }
      else if(ext[fi])
      {
        dl->AddCircleFilled(ImVec2(vfx + vfw * 0.5f, fy + fh - 5.0f), 2.5f, pkt_series_color(ci(fi), 0.9f));
      }
    }
  }

  {
    int edge_hl = es.resize_active ? es.resize_fi : edge_fi;
    if(edge_hl >= 0)
    {
      const ImU32 ecol = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
      for(auto &r : frects)
      {
        if(r.fi != edge_hl || r.fe != wf[r.fi].end) continue;
        float rx = r.x + r.w;
        dl->AddLine(ImVec2(rx, r.y - 1), ImVec2(rx, r.y + fh + 1), ecol, 2.5f);
        break;
      }
    }
  }

  if(any_ext && cfg.showLegend)
  {
    float lx = orig.x + outer;
    float ly = y_base + static_cast<float>(n_rows) * row_step + py;
    float right_edge = orig.x + cw - outer;
    for(int fi = 0; fi < NF; ++fi)
    {
      if(!ext[fi]) continue;
      float iw = sq + lgap + ImGui::CalcTextSize(wf[fi].name.c_str()).x + igap;
      if(lx + iw > right_edge && lx > orig.x + outer) { lx = orig.x + outer; ly += 18.0f; }
      dl->AddRectFilled(ImVec2(lx, ly + 2), ImVec2(lx + sq, ly + sq + 2), pkt_series_color(ci(fi), 0.85f), 2.0f);
      dl->AddText(ImVec2(lx + sq + lgap, ly), tcol, wf[fi].name.c_str());
      lx += iw;
    }
  }

  if(es.resize_active)
  {
    int fi = es.resize_fi;
    if(fi < static_cast<int>(es.drag_fields.size()))
    {
      const auto &hf = es.drag_fields[fi];
      int nbits = hf.end - hf.start + 1;
      ImGui::SetTooltip("%s\n%db", hf.name.c_str(), nbits);
    }
  }
  else if(pkt_hovered && hovered_fi >= 0 && !es.drag_active)
  {
    const auto &hf = wf[hovered_fi];
    int nbits = hf.end - hf.start + 1;
    ImGui::SetTooltip("%s\n[%d-%d]  %db", hf.name.c_str(), hf.start, hf.end, nbits);
  }

  bool open_rename = false, open_add = false;
  if(pkt_hovered && !es.drag_active && !es.resize_active && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
  {
    g_consumed_right_click = true;
    if(hovered_fi >= 0)
    {
      es.ctx_fi = hovered_fi;
      ImGui::OpenPopup("##pkt_ctx");
    }
    else
    {
      es.cfg_edit = d.config;
      ImGui::OpenPopup("##pkt_cfg");
    }
  }

  if(ImGui::BeginPopup("##pkt_ctx"))
  {
    const int fi = es.ctx_fi;
    if(fi >= 0 && fi < static_cast<int>(d.fields.size()))
    {
      char hdr[128];
      std::snprintf(hdr, sizeof(hdr), "%s  [%d-%d]  %db",
                    d.fields[fi].name.c_str(), d.fields[fi].start, d.fields[fi].end,
                    d.fields[fi].end - d.fields[fi].start + 1);
      ImGui::TextDisabled("%s", hdr);
      ImGui::Separator();
      if(ImGui::MenuItem("Rename..."))
      {
        es.rename_fi = fi;
        std::strncpy(es.rename_buf, d.fields[fi].name.c_str(), 255);
        es.rename_buf[255] = '\0';
        es.rename_focus = true;
        open_rename = true;
      }
      ImGui::Separator();
      if(ImGui::MenuItem("Expand +1 bit"))
      {
        PacketDiagram nd = d;
        nd.fields[fi].end++;
        for(int k = fi + 1; k < static_cast<int>(nd.fields.size()); ++k)
        {
          nd.fields[k].start++;
          nd.fields[k].end++;
        }
        nd.total_bits++;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      bool can_shrink = d.fields[fi].end > d.fields[fi].start;
      if(ImGui::MenuItem("Shrink -1 bit", nullptr, false, can_shrink))
      {
        PacketDiagram nd = d;
        nd.fields[fi].end--;
        for(int k = fi + 1; k < static_cast<int>(nd.fields.size()); ++k)
        {
          nd.fields[k].start--;
          nd.fields[k].end--;
        }
        nd.total_bits--;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::Separator();
      if(ImGui::MenuItem("Add field before"))
      {
        es.add_after = fi - 1;
        es.add_bits = 8;
        es.add_name[0] = '\0';
        es.add_focus = true;
        open_add = true;
      }
      if(ImGui::MenuItem("Add field after"))
      {
        es.add_after = fi;
        es.add_bits = 8;
        es.add_name[0] = '\0';
        es.add_focus = true;
        open_add = true;
      }
      ImGui::Separator();
      if(ImGui::MenuItem("Delete field"))
      {
        PacketDiagram nd = d;
        nd.fields.erase(nd.fields.begin() + fi);
        rebuild_bits(nd.fields);
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
    }
    ImGui::EndPopup();
  }
  if(open_rename) ImGui::OpenPopup("##pkt_rename");
  if(open_add) ImGui::OpenPopup("##pkt_add");

  if(ImGui::BeginPopup("##pkt_cfg"))
  {
    ImGui::Text("Packet Config");
    ImGui::Separator();
    ImGui::SliderFloat("Bit Width", &es.cfg_edit.bitWidth, 8.f, 80.f, "%.0fpx");
    ImGui::SliderFloat("Row Height", &es.cfg_edit.rowHeight, 16.f, 120.f, "%.0fpx");
    ImGui::SliderInt("Bits / Row", &es.cfg_edit.bitsPerRow, 4, 128);
    ImGui::SliderFloat("Padding X", &es.cfg_edit.paddingX, 0.f, 40.f, "%.0fpx");
    ImGui::SliderFloat("Padding Y", &es.cfg_edit.paddingY, 0.f, 40.f, "%.0fpx");
    ImGui::Checkbox("Show bit numbers", &es.cfg_edit.showBits);
    ImGui::Checkbox("Show legend", &es.cfg_edit.showLegend);
    ImGui::Separator();
    if(ImGui::Button("Apply"))
    {
      PacketDiagram nd = d;
      nd.config = es.cfg_edit;
      g_pending_edit = {id, serialize_packet(nd)};
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopupModal("##pkt_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Rename field:");
    if(es.rename_focus) { ImGui::SetKeyboardFocusHere(); es.rename_focus = false; }
    bool ok = ImGui::InputText("##rn", es.rename_buf, sizeof(es.rename_buf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if(ImGui::Button("OK") || ok)
    {
      if(es.rename_fi >= 0 && es.rename_fi < static_cast<int>(d.fields.size()) && es.rename_buf[0])
      {
        PacketDiagram nd = d;
        nd.fields[es.rename_fi].name = es.rename_buf;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopupModal("##pkt_add", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("New field:");
    if(es.add_focus) { ImGui::SetKeyboardFocusHere(); es.add_focus = false; }
    ImGui::InputText("Name##an", es.add_name, sizeof(es.add_name));
    ImGui::InputInt("Bits##ab", &es.add_bits);
    if(es.add_bits < 1) es.add_bits = 1;
    ImGui::Spacing();
    if(ImGui::Button("Add"))
    {
      if(es.add_name[0])
      {
        PacketDiagram nd = d;
        PacketField nf;
        nf.name = es.add_name;
        nf.start = 0;
        nf.end = es.add_bits - 1;
        int ins = std::max(0, std::min(es.add_after + 1, static_cast<int>(nd.fields.size())));
        nd.fields.insert(nd.fields.begin() + ins, nf);
        rebuild_bits(nd.fields);
        nd.total_bits = nd.fields.empty() ? 0 : nd.fields.back().end + 1;
        g_pending_edit = {id, serialize_packet(nd)};
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::PopID();
}
} // namespace MermaidDiagrams
