#include "markdown_support.hpp"

#include "markdown_sections.hpp"
#include "markdown_view.hpp"
#include "mermaid_flowchart.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace MarkdownSupport
{
namespace
{
bool extract_checklist_prefix(std::string_view line, std::string &prefix_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size()) return false;
  const size_t indent_end = i;

  const char bullet = line[i];
  if(bullet != '-' && bullet != '*') return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;
  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;

  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;

  prefix_out.assign(line.substr(0, indent_end));
  prefix_out.push_back(bullet);
  prefix_out.append(" [ ] ");
  return true;
}

bool extract_quote_prefix(std::string_view line, std::string &prefix_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size()) return false;
  const size_t indent_end = i;
  if(line[i] != '>') return false;
  ++i;
  if(i < line.size() && line[i] == ' ') ++i;

  prefix_out.assign(line.substr(0, indent_end));
  prefix_out.append("> ");
  return true;
}

bool is_empty_checklist_line(std::string_view line)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size()) return false;
  const char bullet = line[i];
  if(bullet != '-' && bullet != '*') return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;
  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;
  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;
  return NoteCore::trim(line.substr(i)).empty();
}

bool is_empty_quote_line(std::string_view line)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size() || line[i] != '>') return false;
  ++i;
  if(i < line.size() && line[i] == ' ') ++i;
  return NoteCore::trim(line.substr(i)).empty();
}

bool is_word_char(char c)
{
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '_';
}

struct MermaidPieSlice
{
  std::string label;
  float value = 0.0f;
};

struct MermaidPieChart
{
  std::string title;
  std::vector<MermaidPieSlice> slices;
};

bool parse_mermaid_pie(std::string_view body, MermaidPieChart &out)
{
  out = MermaidPieChart{};
  bool saw_pie = false;

  size_t p = 0;
  while(p < body.size())
  {
    size_t e = body.find('\n', p);
    if(e == std::string_view::npos) e = body.size();
    std::string_view line = NoteCore::trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;

    if(!saw_pie)
    {
      if(!NoteCore::starts_with(line, "pie")) return false;
      saw_pie = true;
      const std::string_view rest = NoteCore::trim(line.substr(3));
      if(NoteCore::starts_with(rest, "title "))
        out.title = std::string(NoteCore::trim(rest.substr(6)));
      continue;
    }

    if(NoteCore::starts_with(line, "title "))
    {
      out.title = std::string(NoteCore::trim(line.substr(6)));
      continue;
    }

    const size_t col = line.find(':');
    if(col == std::string_view::npos) continue;

    std::string_view left = NoteCore::trim(line.substr(0, col));
    const std::string_view right = NoteCore::trim(line.substr(col + 1));
    if(left.empty() || right.empty()) continue;

    if(left.size() >= 2 && left.front() == '"' && left.back() == '"')
      left = left.substr(1, left.size() - 2);

    std::string right_s(right);
    char *end = nullptr;
    const float v = std::strtof(right_s.c_str(), &end);
    if(end == right_s.c_str() || v <= 0.0f) continue;

    out.slices.push_back({std::string(left), v});
  }

  return saw_pie && !out.slices.empty();
}

void render_mermaid_placeholder(std::string_view type, std::string_view body, int id)
{
  ImGui::PushID(id);
  ImGui::BeginGroup();
  ImGui::Text("Mermaid: %.*s", static_cast<int>(type.size()), type.data());
  ImGui::Separator();
  ImGui::TextWrapped("%.*s", static_cast<int>(body.size()), body.data());
  ImGui::EndGroup();
  ImGui::PopID();
}

bool is_known_mermaid_type(std::string_view token)
{
  const std::string t = NoteCore::to_lower_copy(token);
  return t == "flowchart" || t == "graph" ||
         t == "sequencediagram" ||
         t == "classdiagram" ||
         t == "statediagram" || t == "statediagram-v2" ||
         t == "erdiagram" ||
         t == "journey" ||
         t == "gantt" ||
         t == "pie" ||
         t == "quadrantchart" ||
         t == "requirementdiagram" ||
         t == "gitgraph" ||
         t == "c4context" || t == "c4container" || t == "c4component" || t == "c4dynamic" || t == "c4deployment" ||
         t == "mindmap" ||
         t == "timeline" ||
         t == "zenuml" ||
         t == "sankey-beta" ||
         t == "xychart-beta" ||
         t == "block-beta" ||
         t == "packet-beta" ||
         t == "kanban" ||
         t == "architecture-beta" ||
         t == "radar-beta" ||
         t == "treemap";
}

bool detect_mermaid_type(std::string_view body, std::string &type_out)
{
  size_t p = 0;
  while(p < body.size())
  {
    size_t e = body.find('\n', p);
    if(e == std::string_view::npos) e = body.size();
    const std::string_view line = NoteCore::trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;
    if(NoteCore::starts_with(line, "%%")) continue;
    if(NoteCore::starts_with(line, "%%{")) continue;

    const size_t sp = line.find_first_of(" \t");
    const std::string_view token = (sp == std::string_view::npos) ? line : line.substr(0, sp);
    if(!is_known_mermaid_type(token)) return false;
    type_out = std::string(token);
    return true;
  }
  return false;
}

void render_mermaid_pie_chart(const MermaidPieChart &chart, int id)
{
  if(!chart.title.empty()) ImGui::TextUnformatted(chart.title.c_str());

  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float chart_w = std::floor(std::max(120.0f, std::min(240.0f, avail_w * 0.45f)));
  const float chart_h = chart_w;

  ImGui::PushID(id);
  ImGui::BeginGroup();
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##pie_canvas", ImVec2(chart_w, chart_h));
  ImGui::EndGroup();

  ImGui::SameLine();
  ImGui::BeginGroup();

  const float radius = chart_w * 0.5f - 2.0f;
  const ImVec2 center(canvas_pos.x + chart_w * 0.5f, canvas_pos.y + chart_h * 0.5f);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  float total = 0.0f;
  for(const auto &s : chart.slices) total += s.value;
  if(total <= 0.0f) total = 1.0f;

  float a0 = -3.14159265f * 0.5f;
  for(size_t i = 0; i < chart.slices.size(); ++i)
  {
    const auto &s = chart.slices[i];
    const float frac = s.value / total;
    const float a1 = a0 + frac * 2.0f * 3.14159265f;

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(
        static_cast<float>(i) / std::max(1.0f, static_cast<float>(chart.slices.size())),
        0.65f,
        0.95f,
        r,
        g,
        b);
    const ImU32 col = ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));

    const int seg = std::max(6, static_cast<int>(36.0f * frac));
    std::vector<ImVec2> pts;
    pts.reserve(static_cast<size_t>(seg) + 2);
    pts.push_back(center);
    for(int j = 0; j <= seg; ++j)
    {
      const float t = a0 + (a1 - a0) * (static_cast<float>(j) / static_cast<float>(seg));
      pts.push_back(ImVec2(center.x + std::cos(t) * radius, center.y + std::sin(t) * radius));
    }
    dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), col);

    ImGui::PushID(static_cast<int>(i));
    ImGui::ColorButton("##c", ImVec4(r, g, b, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(10, 10));
    ImGui::SameLine();
    ImGui::Text("%s : %.2f", s.label.c_str(), s.value);
    ImGui::PopID();

    a0 = a1;
  }

  dl->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), 0, 1.0f);

  ImGui::EndGroup();
  ImGui::PopID();
}

void render_mermaid_block(std::string_view mermaid_type, std::string_view body, int id)
{
  const std::string mt = NoteCore::to_lower_copy(mermaid_type);
  if(mt == "pie")
  {
    MermaidPieChart pie;
    if(parse_mermaid_pie(body, pie))
      render_mermaid_pie_chart(pie, id);
    else
      render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }

  if(mt == "flowchart" || mt == "graph")
  {
    MermaidFlowchart::Graph g;
    if(MermaidFlowchart::parse(body, g))
      MermaidFlowchart::render(g, id);
    else
      render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }

  render_mermaid_placeholder(mermaid_type, body, id);
}
} // namespace

void insert_checklist_item_at_cursor(std::string &text, MdFormatState &fmt)
{
  int p = std::max(0, std::min(fmt.cursor_pos, static_cast<int>(text.size())));
  std::string ins = "- [ ] ";
  if(p > 0 && text[static_cast<size_t>(p) - 1] != '\n') ins = "\n" + ins;
  text.insert(static_cast<size_t>(p), ins);
  p += static_cast<int>(ins.size());
  fmt.cursor_pos = p;
  fmt.sel_start = p;
  fmt.sel_end = p;
}

void apply_note_quote(std::string &s, int &sel_a, int &sel_b)
{
  int a = sel_a;
  int b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, static_cast<int>(s.size())));
  b = std::max(0, std::min(b, static_cast<int>(s.size())));

  while(a > 0 && s[static_cast<size_t>(a) - 1] != '\n') --a;
  while(b < static_cast<int>(s.size()) && s[static_cast<size_t>(b)] != '\n') ++b;

  int offset = 0;
  for(int i = a; i <= b;)
  {
    const int insert_pos = i + offset;
    s.insert(static_cast<size_t>(insert_pos), "> ");
    offset += 2;

    const size_t nl = s.find('\n', static_cast<size_t>(insert_pos + 2));
    if(nl == std::string::npos) break;
    i = static_cast<int>(nl) + 1 - offset;
    if(i > b) break;
  }

  sel_a = a;
  sel_b = b + offset;
}

void apply_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &left, const std::string &right)
{
  int a = sel_a;
  int b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, static_cast<int>(s.size())));
  b = std::max(0, std::min(b, static_cast<int>(s.size())));

  s.insert(static_cast<size_t>(b), right);
  s.insert(static_cast<size_t>(a), left);

  a += static_cast<int>(left.size());
  b += static_cast<int>(left.size());
  sel_a = a;
  sel_b = b;
}

void apply_color_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &hex_color)
{
  int a = sel_a;
  int b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, static_cast<int>(s.size())));
  b = std::max(0, std::min(b, static_cast<int>(s.size())));

  while(b > a && (s[static_cast<size_t>(b) - 1] == '\n' || s[static_cast<size_t>(b) - 1] == '\r')) --b;
  if(a == b) return;

  sel_a = a;
  sel_b = b;
  apply_wrap_string(s, sel_a, sel_b, "[color=" + hex_color + "]", "[/color]");
}

std::string rgba_to_hex(ImVec4 c)
{
  const int r = static_cast<int>(NoteCore::clamp01f(c.x) * 255.0f + 0.5f);
  const int g = static_cast<int>(NoteCore::clamp01f(c.y) * 255.0f + 0.5f);
  const int b = static_cast<int>(NoteCore::clamp01f(c.z) * 255.0f + 0.5f);

  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return std::string(buf);
}

std::pair<int, int> line_bounds_from_cursor(const std::string &text, int cursor_pos)
{
  int c = std::max(0, std::min(cursor_pos, static_cast<int>(text.size())));
  int line_start = c;
  while(line_start > 0 && text[static_cast<size_t>(line_start) - 1] != '\n') --line_start;

  int line_end = c;
  while(line_end < static_cast<int>(text.size()) && text[static_cast<size_t>(line_end)] != '\n') ++line_end;
  return {line_start, line_end};
}

bool should_push_word_granular_undo(const std::string &before, const std::string &after, MdFormatState &st)
{
  const size_t nb = before.size();
  const size_t na = after.size();

  auto reset_groups = [&]() {
    st.typing_word_group = false;
    st.deleting_word_group = false;
  };

  if(before == after) return false;

  size_t i = 0;
  while(i < nb && i < na && before[i] == after[i]) ++i;

  if(na == nb + 1)
  {
    const char c = after[i];
    st.deleting_word_group = false;
    if(!is_word_char(c))
    {
      st.typing_word_group = false;
      st.last_edit_cursor = st.cursor_pos;
      return false;
    }

    const bool contiguous = (st.last_edit_cursor >= 0 && st.cursor_pos == st.last_edit_cursor + 1);
    const bool start_group = !st.typing_word_group || !contiguous;
    st.typing_word_group = true;
    st.last_edit_cursor = st.cursor_pos;
    return start_group;
  }

  if(nb == na + 1)
  {
    const char c = before[i];
    st.typing_word_group = false;
    if(!is_word_char(c))
    {
      st.deleting_word_group = false;
      st.last_edit_cursor = st.cursor_pos;
      return false;
    }

    const bool contiguous =
        (st.last_edit_cursor >= 0) &&
        (st.cursor_pos == st.last_edit_cursor || st.cursor_pos == st.last_edit_cursor - 1);
    const bool start_group = !st.deleting_word_group || !contiguous;
    st.deleting_word_group = true;
    st.last_edit_cursor = st.cursor_pos;
    return start_group;
  }

  reset_groups();
  st.last_edit_cursor = st.cursor_pos;
  return true;
}

int md_editor_cb(ImGuiInputTextCallbackData *data)
{
  auto *st = static_cast<MdFormatState *>(data->UserData);

  if(st->pending_select_range)
  {
    const int a = std::max(0, std::min(st->pending_sel_start, data->BufTextLen));
    const int b = std::max(0, std::min(st->pending_sel_end, data->BufTextLen));
    data->SelectionStart = a;
    data->SelectionEnd = b;
    data->CursorPos = b;
    st->sel_start = a;
    st->sel_end = b;
    st->cursor_pos = b;
    st->pending_select_range = false;
  }

  st->sel_start = data->SelectionStart;
  st->sel_end = data->SelectionEnd;
  st->cursor_pos = data->CursorPos;
  if(st->sel_start == st->sel_end) st->selection_anchor = st->cursor_pos;
  st->last_cursor_pos = st->cursor_pos;

  if(data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
  {
    const int c = data->CursorPos;
    if(c > 0 && data->Buf[static_cast<size_t>(c) - 1] == '\n')
    {
      const int line_end = c - 1;
      int line_start = line_end - 1;
      while(line_start >= 0 && data->Buf[static_cast<size_t>(line_start)] != '\n') --line_start;
      ++line_start;

      const std::string_view prev(data->Buf + line_start, static_cast<size_t>(line_end - line_start));
      std::string prefix;
      if(extract_checklist_prefix(prev, prefix))
      {
        if(is_empty_checklist_line(prev))
        {
          data->DeleteChars(line_start, line_end - line_start);
          data->CursorPos = line_start + 1;
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
        else
        {
          data->InsertChars(c, prefix.c_str());
          data->CursorPos = c + static_cast<int>(prefix.size());
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
      }
      else if(extract_quote_prefix(prev, prefix))
      {
        if(is_empty_quote_line(prev))
        {
          data->DeleteChars(line_start, line_end - line_start);
          data->CursorPos = line_start + 1;
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
        else
        {
          data->InsertChars(c, prefix.c_str());
          data->CursorPos = c + static_cast<int>(prefix.size());
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
      }
    }
  }

  return 0;
}

void normalize_input_text_buffer(std::string &s)
{
  if(s.empty()) return;
  const size_t max_len = s.capacity() + 1;
  const size_t n = strnlen(s.data(), max_len);
  if(n <= s.size() || n <= s.capacity()) s.resize(n);
}

bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

  if(i >= line.size() || (line[i] != '-' && line[i] != '*')) return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;

  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;

  check_col_out = i + 1;
  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;
  label_out = line.substr(i);
  return true;
}

bool render_preview_with_task_checkboxes(std::string &markdown)
{
  bool changed = false;
  std::string normal_chunk;
  normal_chunk.reserve(markdown.size());

  struct HeaderUi
  {
    int level = 0;
    bool open = false;
  };

  std::vector<HeaderUi> header_stack;

  auto flush_chunk = [&]() {
    if(normal_chunk.empty()) return;
    MarkdownView::render(normal_chunk);
    normal_chunk.clear();
  };

  auto all_headers_open = [&]() {
    for(const auto &h : header_stack)
    {
      if(!h.open) return false;
    }
    return true;
  };

  size_t pos = 0;
  while(pos < markdown.size())
  {
    const size_t line_start = pos;
    size_t line_end = markdown.find('\n', pos);
    const bool has_newline = (line_end != std::string::npos);
    if(!has_newline) line_end = markdown.size();

    const std::string_view line(markdown.data() + line_start, line_end - line_start);
    const std::string_view tline = NoteCore::trim(line);

    int heading_level = 0;
    std::string_view heading_title;
    if(parse_heading_line(line, heading_level, heading_title))
    {
      flush_chunk();
      while(!header_stack.empty() && header_stack.back().level >= heading_level)
      {
        if(header_stack.back().open) ImGui::TreePop();
        header_stack.pop_back();
      }
      if(!all_headers_open())
      {
        pos = has_newline ? line_end + 1 : line_end;
        continue;
      }
      const bool open = ImGui::TreeNodeEx(
          reinterpret_cast<void *>(static_cast<intptr_t>(static_cast<int>(line_start) + 0x10000)),
          ImGuiTreeNodeFlags_SpanAvailWidth,
          "%s",
          std::string(heading_title).c_str());
      header_stack.push_back(HeaderUi{heading_level, open});
      pos = has_newline ? line_end + 1 : line_end;
      continue;
    }

    if(!all_headers_open())
    {
      pos = has_newline ? line_end + 1 : line_end;
      continue;
    }

    if(tline == "```mermaid")
    {
      size_t scan = has_newline ? line_end + 1 : line_end;
      size_t block_end = markdown.size();
      std::string body;
      bool closed = false;

      while(scan < markdown.size())
      {
        const size_t ls = scan;
        size_t le = markdown.find('\n', scan);
        const bool ln = (le != std::string::npos);
        if(!ln) le = markdown.size();

        const std::string_view l(markdown.data() + ls, le - ls);
        if(NoteCore::trim(l) == "```")
        {
          block_end = ln ? le + 1 : le;
          closed = true;
          break;
        }
        body.append(l.data(), l.size());
        body.push_back('\n');
        scan = ln ? le + 1 : le;
      }

      if(closed)
      {
        std::string mermaid_type;
        if(detect_mermaid_type(body, mermaid_type))
        {
          flush_chunk();
          render_mermaid_block(mermaid_type, body, static_cast<int>(line_start));
        }
        else
        {
          normal_chunk.append(markdown.data() + line_start, block_end - line_start);
        }
        pos = block_end;
        continue;
      }
    }

    {
      const size_t sp = tline.find_first_of(" \t");
      const std::string_view maybe_type = (sp == std::string_view::npos) ? tline : tline.substr(0, sp);
      if(is_known_mermaid_type(maybe_type))
      {
        size_t scan = line_start;
        size_t block_end = markdown.size();
        std::string body;

        while(scan < markdown.size())
        {
          const size_t ls = scan;
          size_t le = markdown.find('\n', scan);
          const bool ln = (le != std::string::npos);
          if(!ln) le = markdown.size();
          const std::string_view l(markdown.data() + ls, le - ls);
          const std::string_view tl = NoteCore::trim(l);

          if(tl.empty())
          {
            block_end = ln ? le + 1 : le;
            break;
          }
          body.append(l.data(), l.size());
          body.push_back('\n');
          scan = ln ? le + 1 : le;
          block_end = scan;
        }

        std::string mermaid_type;
        if(detect_mermaid_type(body, mermaid_type))
        {
          flush_chunk();
          render_mermaid_block(mermaid_type, body, static_cast<int>(line_start));
          pos = block_end;
          continue;
        }
      }
    }

    size_t check_col = 0;
    std::string_view label;
    if(parse_task_line(line, check_col, label))
    {
      flush_chunk();

      bool checked = (line[check_col] == 'x' || line[check_col] == 'X');
      const ImVec2 item_sp = ImGui::GetStyle().ItemSpacing;
      const ImVec2 frame_pad = ImGui::GetStyle().FramePadding;
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_sp.x, 2.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_pad.x, 1.0f));
      ImGui::PushID(static_cast<int>(line_start));
      if(ImGui::Checkbox("##task", &checked))
      {
        markdown[line_start + check_col] = checked ? 'x' : ' ';
        changed = true;
      }
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      MarkdownView::render_inline(std::string(label));
      ImGui::PopID();
      ImGui::PopStyleVar(2);
    }
    else
    {
      normal_chunk.append(line.data(), line.size());
      if(has_newline) normal_chunk.push_back('\n');
    }

    pos = has_newline ? line_end + 1 : line_end;
  }

  flush_chunk();
  while(!header_stack.empty())
  {
    if(header_stack.back().open) ImGui::TreePop();
    header_stack.pop_back();
  }
  return changed;
}
} // namespace MarkdownSupport
