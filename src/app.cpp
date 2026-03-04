#include "app.hpp"
#include "helpers.hpp"
#include "markdown_view.hpp"
#include "mermaid_flowchart.hpp"
#include "mermaid_flowchart.cpp"

#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <filesystem>

#include <SDL.h>
#include <SDL_opengl.h>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

namespace
{
constexpr const char *kGlslVersion = "#version 150";
constexpr const char *kDefaultStateFile = DATA_PATH "/note.md";
constexpr const char *kLegacyStateMetaFile = DATA_PATH "/current_note_path.txt";
constexpr const char *kIndexFile = DATA_PATH "/notes_index.json";
struct MdSection
{
  int level = 0;               // 1..6
  std::string title;           // heading text
  std::string body;            // markdown until next heading of same/higher level
  std::vector<MdSection> kids; // nested headings
};

struct MdFormatState
{
  int sel_start = 0;
  int sel_end = 0;
  int cursor_pos = 0;

  enum class Action
  {
    None,
    Italic,
    Bold,
    Strike,
    Code,
    Color
  } pending = Action::None;
  ImVec4 color = ImVec4(1, 0.6f, 0.2f, 1); // default
};

struct MdEditorUserData
{
  std::string *text = nullptr;
  MdFormatState *fmt = nullptr;
};

static bool extract_checklist_prefix(std::string_view line, std::string &prefix_out)
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

static bool is_empty_checklist_line(std::string_view line)
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
  return trim(line.substr(i)).empty();
}

static void insert_checklist_item_at_cursor(std::string &text, MdFormatState &fmt)
{
  int p = std::max(0, std::min(fmt.cursor_pos, (int)text.size()));
  std::string ins = "- [ ] ";
  if(p > 0 && text[(size_t)p - 1] != '\n') ins = "\n" + ins;
  text.insert((size_t)p, ins);
  p += (int)ins.size();
  fmt.cursor_pos = p;
  fmt.sel_start = p;
  fmt.sel_end = p;
}

static void apply_note_quote(std::string &s, int &sel_a, int &sel_b)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Expand to full lines for nicer behavior
  while(a > 0 && s[(size_t)a - 1] != '\n') --a;
  while(b < (int)s.size() && s[(size_t)b] != '\n') ++b;

  // Count lines and insert "> " at each line start.
  // We insert from start to end while tracking the shifting offset.
  int offset = 0;
  for(int i = a; i <= b;)
  {
    const int insert_pos = i + offset;
    s.insert((size_t)insert_pos, "> ");
    offset += 2;

    // Move to next line start
    size_t nl = s.find('\n', (size_t)(insert_pos + 2));
    if(nl == std::string::npos) break;
    i = (int)nl + 1 - offset; // convert back to original coordinate space
    if(i > b) break;
  }

  // Update selection to include the inserted prefixes
  sel_a = a;
  sel_b = b + offset;
}

static void apply_wrap_string(std::string &s, int &sel_a, int &sel_b,
                              const std::string &left, const std::string &right)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Insert right first (at higher index)
  s.insert((size_t)b, right);
  s.insert((size_t)a, left);

  // Update selection to remain around the original content
  a += (int)left.size();
  b += (int)left.size();
  sel_a = a;
  sel_b = b;
}

static void apply_color_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &hex_color)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);
  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Keep trailing EOL outside color tags to avoid accidental visual line jumps.
  while(b > a && (s[(size_t)b - 1] == '\n' || s[(size_t)b - 1] == '\r')) --b;
  if(a == b) return;

  sel_a = a;
  sel_b = b;
  apply_wrap_string(s, sel_a, sel_b, "[color=" + hex_color + "]", "[/color]");
}

static std::string rgba_to_hex(ImVec4 c)
{
  auto clamp01 = [](float v) { return v < 0 ? 0.f : (v > 1 ? 1.f : v); };
  int r = (int)(clamp01(c.x) * 255.0f + 0.5f);
  int g = (int)(clamp01(c.y) * 255.0f + 0.5f);
  int b = (int)(clamp01(c.z) * 255.0f + 0.5f);

  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return std::string(buf);
}

static int md_editor_cb(ImGuiInputTextCallbackData *data)
{
  auto *st = static_cast<MdFormatState *>(data->UserData);

  // Track selection continuously
  st->sel_start = data->SelectionStart;
  st->sel_end = data->SelectionEnd;
  st->cursor_pos = data->CursorPos;

  if(data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
  {
    const int c = data->CursorPos;
    if(c > 0 && data->Buf[(size_t)c - 1] == '\n')
    {
      const int line_end = c - 1;
      int line_start = line_end - 1;
      while(line_start >= 0 && data->Buf[(size_t)line_start] != '\n') --line_start;
      ++line_start;

      std::string_view prev(data->Buf + line_start, (size_t)(line_end - line_start));
      std::string prefix;
      if(extract_checklist_prefix(prev, prefix))
      {
        if(is_empty_checklist_line(prev))
        {
          // Enter on empty checklist item exits the list: remove the marker from previous line.
          data->DeleteChars(line_start, line_end - line_start);
          data->CursorPos = line_start + 1; // keep cursor after the newline
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
        else
        {
          data->InsertChars(c, prefix.c_str());
          data->CursorPos = c + (int)prefix.size();
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
      }
    }
  }

  // // Apply a pending action inside the callback (safe)
  // if(st->pending != MdFormatState::Action::None)
  // {
  //   switch(st->pending)
  //   {
  //   case MdFormatState::Action::Italic:
  //     apply_wrap(data, "*", "*");
  //     break;
  //   case MdFormatState::Action::Bold:
  //     apply_wrap(data, "**", "**");
  //     break;
  //   case MdFormatState::Action::Strike:
  //     apply_wrap(data, "~~", "~~");
  //     break;
  //   case MdFormatState::Action::Code:
  //     apply_wrap(data, "`", "`");
  //     break;
  //   case MdFormatState::Action::Color: {
  //     const std::string hex = rgba_to_hex(st->color);
  //     const std::string left = "[color=" + hex + "]";
  //     const char *right = "[/color]";
  //     apply_wrap(data, left.c_str(), right);
  //   }
  //   break;
  //   default:
  //     break;
  //   }

  //   st->pending = MdFormatState::Action::None;
  // }

  return 0;
}

static void normalize_input_text_buffer(std::string &s)
{
  // ImGui edits the underlying char buffer directly; keep std::string::size() in sync.
  if(s.empty()) return;
  const size_t max_len = s.capacity() + 1;
  const size_t n = strnlen(s.data(), max_len);
  if(n <= s.size() || n <= s.capacity()) s.resize(n);
}

static std::string sanitize_note_filename(std::string title)
{
  for(char &c : title)
  {
    const bool bad =
        c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|';
    if(bad) c = '_';
  }

  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while(!title.empty() && is_space(title.front())) title.erase(title.begin());
  while(!title.empty() && is_space(title.back())) title.pop_back();
  if(title.empty()) title = "note";
  return title;
}

static std::string json_escape(std::string_view s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for(char c : s)
  {
    switch(c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

static std::string json_unescape(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for(size_t i = 0; i < s.size(); ++i)
  {
    if(s[i] == '\\' && i + 1 < s.size())
    {
      char n = s[i + 1];
      if(n == 'n')
        out.push_back('\n');
      else if(n == 'r')
        out.push_back('\r');
      else if(n == 't')
        out.push_back('\t');
      else
        out.push_back(n);
      ++i;
    }
    else
    {
      out.push_back(s[i]);
    }
  }
  return out;
}

static size_t find_matching(std::string_view s, size_t start, char open, char close)
{
  if(start >= s.size() || s[start] != open) return std::string::npos;
  int depth = 0;
  bool in_string = false;
  for(size_t i = start; i < s.size(); ++i)
  {
    char c = s[i];
    if(c == '"' && (i == 0 || s[i - 1] != '\\')) in_string = !in_string;
    if(in_string) continue;
    if(c == open)
      ++depth;
    else if(c == close)
    {
      --depth;
      if(depth == 0) return i;
    }
  }
  return std::string::npos;
}

static std::string json_find_string(std::string_view obj, std::string_view key)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t k = obj.find(pat);
  if(k == std::string::npos) return {};
  size_t q1 = obj.find('"', k + pat.size());
  if(q1 == std::string::npos) return {};
  size_t q2 = q1 + 1;
  while(q2 < obj.size())
  {
    if(obj[q2] == '"' && obj[q2 - 1] != '\\') break;
    ++q2;
  }
  if(q2 >= obj.size()) return {};
  return json_unescape(obj.substr(q1 + 1, q2 - q1 - 1));
}

static int json_find_int(std::string_view obj, std::string_view key, int defv)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t k = obj.find(pat);
  if(k == std::string::npos) return defv;
  size_t c = obj.find(':', k + pat.size());
  if(c == std::string::npos) return defv;
  size_t b = c + 1;
  while(b < obj.size() && (obj[b] == ' ' || obj[b] == '\t' || obj[b] == '\n' || obj[b] == '\r')) ++b;
  size_t e = b;
  while(e < obj.size() && (obj[e] == '-' || (obj[e] >= '0' && obj[e] <= '9'))) ++e;
  if(e <= b) return defv;
  return std::atoi(std::string(obj.substr(b, e - b)).c_str());
}

static bool json_find_bool(std::string_view obj, std::string_view key, bool defv)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t k = obj.find(pat);
  if(k == std::string::npos) return defv;
  size_t c = obj.find(':', k + pat.size());
  if(c == std::string::npos) return defv;
  size_t b = c + 1;
  while(b < obj.size() && (obj[b] == ' ' || obj[b] == '\t' || obj[b] == '\n' || obj[b] == '\r')) ++b;
  if(obj.substr(b, 4) == "true") return true;
  if(obj.substr(b, 5) == "false") return false;
  return defv;
}

static std::vector<std::string_view> json_array_objects(std::string_view arr)
{
  std::vector<std::string_view> out;
  size_t p = 0;
  while(p < arr.size())
  {
    size_t b = arr.find('{', p);
    if(b == std::string::npos) break;
    size_t e = find_matching(arr, b, '{', '}');
    if(e == std::string::npos) break;
    out.push_back(arr.substr(b, e - b + 1));
    p = e + 1;
  }
  return out;
}

static bool parse_heading_line(std::string_view line, int &level_out, std::string_view &title_out)
{
  line = ltrim(line);
  int level = 0;
  while(level < 6 && level < (int)line.size() && line[level] == '#') level++;
  if(level == 0) return false;

  // require a space after hashes (common markdown rule)
  if((size_t)level >= line.size() || line[(size_t)level] != ' ') return false;

  std::string_view title = trim(line.substr((size_t)level + 1));
  if(title.empty()) title = "(untitled)";

  level_out = level;
  title_out = title;
  return true;
}

static MdSection parse_sections(std::string_view md)
{
  MdSection root; // level 0
  std::vector<MdSection *> stack;
  stack.push_back(&root);

  size_t pos = 0;
  auto take_line = [&](size_t &p) -> std::string_view {
    if(p >= md.size()) return {};
    size_t e = md.find('\n', p);
    if(e == std::string_view::npos) e = md.size();
    auto line = md.substr(p, e - p);
    p = (e < md.size()) ? e + 1 : e;
    return line;
  };

  while(pos < md.size())
  {
    std::string_view line = take_line(pos);

    int level = 0;
    std::string_view title;
    if(parse_heading_line(line, level, title))
    {
      // pop until parent has lower level
      while(!stack.empty() && stack.back()->level >= level) stack.pop_back();
      if(stack.empty()) stack.push_back(&root);

      // create node under current parent
      stack.back()->kids.push_back(MdSection{level, std::string(title), {}, {}});
      MdSection *added = &stack.back()->kids.back();
      stack.push_back(added);
    }
    else
    {
      // normal content belongs to current section
      stack.back()->body.append(line.data(), line.size());
      stack.back()->body.push_back('\n');
    }
  }

  return root;
}

static bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out)
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

static bool parse_mermaid_pie(std::string_view body, MermaidPieChart &out);
static void render_mermaid_pie_chart(const MermaidPieChart &chart, int id);

static std::string to_lower_copy(std::string_view s)
{
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

static bool is_known_mermaid_type(std::string_view token)
{
  const std::string t = to_lower_copy(token);
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

static bool detect_mermaid_type(std::string_view body, std::string &type_out)
{
  size_t p = 0;
  while(p < body.size())
  {
    size_t e = body.find('\n', p);
    if(e == std::string_view::npos) e = body.size();
    std::string_view line = trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;
    if(starts_with(line, "%%")) continue;  // comment
    if(starts_with(line, "%%{")) continue; // init block

    size_t sp = line.find_first_of(" \t");
    std::string_view token = (sp == std::string_view::npos) ? line : line.substr(0, sp);
    if(!is_known_mermaid_type(token)) return false;
    type_out = std::string(token);
    return true;
  }
  return false;
}

static void render_mermaid_placeholder(std::string_view type, std::string_view body, int id)
{
  ImGui::PushID(id);
  ImGui::BeginGroup();
  ImGui::Text("Mermaid: %.*s", (int)type.size(), type.data());
  ImGui::Separator();
  ImGui::TextWrapped("%.*s", (int)body.size(), body.data());
  ImGui::EndGroup();
  ImGui::PopID();
}

static void render_mermaid_block(std::string_view mermaid_type, std::string_view body, int id)
{
  const std::string mt = to_lower_copy(mermaid_type);
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

static bool parse_mermaid_pie(std::string_view body, MermaidPieChart &out)
{
  out = MermaidPieChart{};
  bool saw_pie = false;

  size_t p = 0;
  while(p < body.size())
  {
    size_t e = body.find('\n', p);
    if(e == std::string_view::npos) e = body.size();
    std::string_view line = trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;

    if(!saw_pie)
    {
      if(!starts_with(line, "pie")) return false;
      saw_pie = true;
      std::string_view rest = trim(line.substr(3));
      if(starts_with(rest, "title "))
        out.title = std::string(trim(rest.substr(6)));
      continue;
    }

    if(starts_with(line, "title "))
    {
      out.title = std::string(trim(line.substr(6)));
      continue;
    }

    size_t col = line.find(':');
    if(col == std::string_view::npos) continue;

    std::string_view left = trim(line.substr(0, col));
    std::string_view right = trim(line.substr(col + 1));
    if(left.empty() || right.empty()) continue;

    if(left.size() >= 2 && left.front() == '"' && left.back() == '"')
      left = left.substr(1, left.size() - 2);

    std::string right_s(right);
    char *end = nullptr;
    float v = std::strtof(right_s.c_str(), &end);
    if(end == right_s.c_str()) continue;
    if(v <= 0.0f) continue;

    out.slices.push_back({std::string(left), v});
  }

  return saw_pie && !out.slices.empty();
}

static void render_mermaid_pie_chart(const MermaidPieChart &chart, int id)
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

    float r = 0, g = 0, b = 0;
    ImGui::ColorConvertHSVtoRGB((float)i / std::max(1.0f, (float)chart.slices.size()), 0.65f, 0.95f, r, g, b);
    const ImU32 col = ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));

    const int seg = std::max(6, (int)(36.0f * frac));
    std::vector<ImVec2> pts;
    pts.reserve((size_t)seg + 2);
    pts.push_back(center);
    for(int j = 0; j <= seg; ++j)
    {
      const float t = a0 + (a1 - a0) * ((float)j / (float)seg);
      pts.push_back(ImVec2(center.x + std::cos(t) * radius, center.y + std::sin(t) * radius));
    }
    dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), col);

    ImGui::PushID((int)i);
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

static bool render_preview_with_task_checkboxes(std::string &markdown)
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

  auto all_headers_open = [&]() -> bool {
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

    std::string_view line(markdown.data() + line_start, line_end - line_start);
    std::string_view tline = trim(line);

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
      ImGuiTreeNodeFlags hf = ImGuiTreeNodeFlags_SpanAvailWidth;
      bool open = ImGui::TreeNodeEx(
          (void *)(intptr_t)((int)line_start + 0x10000),
          hf,
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
        size_t ls = scan;
        size_t le = markdown.find('\n', scan);
        bool ln = (le != std::string::npos);
        if(!ln) le = markdown.size();

        std::string_view l(markdown.data() + ls, le - ls);
        if(trim(l) == "```")
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
          render_mermaid_block(mermaid_type, body, (int)line_start);
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
      size_t sp = tline.find_first_of(" \t");
      std::string_view maybe_type = (sp == std::string_view::npos) ? tline : tline.substr(0, sp);
      if(is_known_mermaid_type(maybe_type))
      {
        size_t scan = line_start;
        size_t block_end = markdown.size();
        std::string body;

        while(scan < markdown.size())
        {
          size_t ls = scan;
          size_t le = markdown.find('\n', scan);
          bool ln = (le != std::string::npos);
          if(!ln) le = markdown.size();
          std::string_view l(markdown.data() + ls, le - ls);
          std::string_view tl = trim(l);

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
          render_mermaid_block(mermaid_type, body, (int)line_start);
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
      ImGui::PushID((int)line_start);
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

} // namespace

int App::run()
{
  try
  {
    init_sdl_gl();
    init_imgui();
    load_state();

    while(running_)
    {
      frame_begin();
      frame_ui();
      frame_end();
    }

    save_state();
    shutdown();
    return 0;
  }
  catch(const std::exception &e)
  {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
    shutdown();
    return 1;
  }
}

void App::init_sdl_gl()
{
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

  // OpenGL 3.2 Core
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  window_ = SDL_CreateWindow(
      "Notepp",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1100, 700,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if(!window_)
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

  gl_context_ = SDL_GL_CreateContext(window_);
  if(!gl_context_)
    throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());

  SDL_GL_MakeCurrent(window_, gl_context_);
  SDL_GL_SetSwapInterval(1); // vsync
}

void App::init_imgui()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();

  // Pick a font family that has Bold/Italic files available.
  io.Fonts->AddFontDefault();

  // Example with Roboto (put these files in assets/fonts or wherever you want):
  // - Roboto-Regular.ttf
  // - Roboto-Italic.ttf
  // - Roboto-Bold.ttf
  // - Roboto-BoldItalic.ttf

  constexpr float kUiFontSize = 14.0f;
  ImFont *font_regular = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Medium.ttf", kUiFontSize);
  ImFont *font_italic = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Italic.ttf", kUiFontSize);
  ImFont *font_bold = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Bold.ttf", kUiFontSize);

  // Fallback if you don’t have files yet:
  if(!font_regular) font_regular = io.Fonts->Fonts.front();
  if(!font_italic) font_italic = font_regular;
  if(!font_bold) font_bold = font_regular;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  if(!ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_))
    throw std::runtime_error("ImGui_ImplSDL2_InitForOpenGL failed");

  if(!ImGui_ImplOpenGL3_Init(kGlslVersion))
    throw std::runtime_error("ImGui_ImplOpenGL3_Init failed");

  MarkdownView::set_fonts(font_regular, font_italic, font_bold);
}

void App::shutdown()
{
  // Safe to call multiple times.
  if(ImGui::GetCurrentContext())
  {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }

  if(gl_context_)
  {
    SDL_GL_DeleteContext(gl_context_);
    gl_context_ = nullptr;
  }

  if(window_)
  {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  SDL_Quit();
}

void App::load_state()
{
  folders_.clear();
  active_folder_idx_ = 0;
  active_note_idx_ = 0;
  folder_overview_mode_ = false;

  std::ifstream in_index(kIndexFile);
  if(in_index)
  {
    const std::string doc((std::istreambuf_iterator<char>(in_index)), std::istreambuf_iterator<char>());
    active_folder_idx_ = json_find_int(doc, "active_folder", 0);
    active_note_idx_ = json_find_int(doc, "active_note", 0);
    folder_overview_mode_ = json_find_bool(doc, "folder_view", false);

    const std::string fpat = "\"folders\"";
    size_t fk = doc.find(fpat);
    if(fk != std::string::npos)
    {
      size_t fb = doc.find('[', fk + fpat.size());
      if(fb != std::string::npos)
      {
        size_t fe = find_matching(doc, fb, '[', ']');
        if(fe != std::string::npos)
        {
          std::string_view folder_arr(doc.data() + fb + 1, fe - fb - 1);
          for(std::string_view fobj : json_array_objects(folder_arr))
          {
            FolderMeta f;
            f.name = json_find_string(fobj, "name");
            if(f.name.empty()) f.name = "General";

            const std::string npat = "\"notes\"";
            size_t nk = fobj.find(npat);
            if(nk != std::string_view::npos)
            {
              size_t nb = fobj.find('[', nk + npat.size());
              if(nb != std::string_view::npos)
              {
                size_t ne = find_matching(fobj, nb, '[', ']');
                if(ne != std::string_view::npos)
                {
                  std::string_view notes_arr = fobj.substr(nb + 1, ne - nb - 1);
                  for(std::string_view nobj : json_array_objects(notes_arr))
                  {
                    NoteMeta n;
                    n.title = json_find_string(nobj, "title");
                    n.path = json_find_string(nobj, "path");
                    n.pos_x = (float)json_find_int(nobj, "x", 0);
                    n.pos_y = (float)json_find_int(nobj, "y", 0);
                    n.width = (float)json_find_int(nobj, "w", 520);
                    n.height = (float)json_find_int(nobj, "h", 260);
                    n.has_layout = json_find_bool(nobj, "has_layout", false);
                    if(n.title.empty()) n.title = "Note";
                    if(n.path.empty()) n.path = make_note_path(f.name, n.title);
                    f.notes.push_back(std::move(n));
                  }
                }
              }
            }
            folders_.push_back(std::move(f));
          }
        }
      }
    }
  }
  else
  {
    // Migration from legacy current_note_path.txt
    std::string migrated_path = kDefaultStateFile;
    std::ifstream legacy(kLegacyStateMetaFile);
    if(legacy)
    {
      std::string p;
      if(std::getline(legacy, p) && !p.empty()) migrated_path = p;
    }

    FolderMeta f;
    f.name = "General";
    NoteMeta n;
    n.path = migrated_path;
    const std::filesystem::path fp(migrated_path);
    n.title = fp.stem().empty() ? "Note" : fp.stem().string();
    f.notes.push_back(std::move(n));
    folders_.push_back(std::move(f));
  }

  ensure_default_index();
  active_folder_idx_ = std::max(0, std::min(active_folder_idx_, (int)folders_.size() - 1));
  active_note_idx_ = std::max(0, std::min(active_note_idx_, (int)folders_[(size_t)active_folder_idx_].notes.size() - 1));
  load_note_content_for_active();
}

void App::save_state() const
{
  std::ofstream out(state_file_path_, std::ios::binary | std::ios::trunc);
  if(!out) return;
  out << markdown_text_;
  save_index();
}

void App::save_index() const
{
  std::ofstream out(kIndexFile, std::ios::trunc);
  if(!out) return;

  out << "{\n";
  out << "  \"active_folder\": " << active_folder_idx_ << ",\n";
  out << "  \"active_note\": " << active_note_idx_ << ",\n";
  out << "  \"folder_view\": " << (folder_overview_mode_ ? "true" : "false") << ",\n";
  out << "  \"folders\": [\n";
  for(size_t fi = 0; fi < folders_.size(); ++fi)
  {
    const auto &f = folders_[fi];
    out << "    {\n";
    out << "      \"name\": \"" << json_escape(f.name) << "\",\n";
    out << "      \"notes\": [\n";
    for(size_t ni = 0; ni < f.notes.size(); ++ni)
    {
      const auto &n = f.notes[ni];
      out << "        {\"title\": \"" << json_escape(n.title)
          << "\", \"path\": \"" << json_escape(n.path)
          << "\", \"x\": " << (int)std::lround(n.pos_x)
          << ", \"y\": " << (int)std::lround(n.pos_y)
          << ", \"w\": " << (int)std::lround(n.width)
          << ", \"h\": " << (int)std::lround(n.height)
          << ", \"has_layout\": " << (n.has_layout ? "true" : "false")
          << "}";
      if(ni + 1 < f.notes.size()) out << ",";
      out << "\n";
    }
    out << "      ]\n";
    out << "    }";
    if(fi + 1 < folders_.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

std::string App::make_note_path(const std::string &folder_name, const std::string &note_title) const
{
  const std::string f = sanitize_note_filename(folder_name);
  const std::string n = sanitize_note_filename(note_title);
  std::filesystem::path dir = std::filesystem::path(DATA_PATH) / "notes" / f;
  return (dir / (n + ".md")).string();
}

void App::ensure_default_index()
{
  if(folders_.empty()) folders_.push_back(FolderMeta{"General", {}});
  for(auto &f : folders_)
  {
    if(f.name.empty()) f.name = "General";
    if(f.notes.empty())
    {
      NoteMeta n;
      n.title = "Note";
      n.path = make_note_path(f.name, n.title);
      f.notes.push_back(std::move(n));
    }
  }

  active_folder_idx_ = std::max(0, std::min(active_folder_idx_, (int)folders_.size() - 1));
  active_note_idx_ = std::max(0, std::min(active_note_idx_, (int)folders_[(size_t)active_folder_idx_].notes.size() - 1));
}

void App::load_note_content_for_active()
{
  ensure_default_index();
  const NoteMeta &n = folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_];
  state_file_path_ = n.path;
  note_title_ = n.title;

  std::ifstream in(state_file_path_, std::ios::binary);
  if(in)
  {
    markdown_text_.assign((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  }
  else
  {
    std::filesystem::create_directories(std::filesystem::path(state_file_path_).parent_path());
    save_state();
  }
  undo_stack_.clear();
  redo_stack_.clear();
  request_undo_edit_ = false;
  request_redo_edit_ = false;
}

void App::set_active_note(int folder_idx, int note_idx)
{
  ensure_default_index();
  folder_idx = std::max(0, std::min(folder_idx, (int)folders_.size() - 1));
  note_idx = std::max(0, std::min(note_idx, (int)folders_[(size_t)folder_idx].notes.size() - 1));

  save_state();
  active_folder_idx_ = folder_idx;
  active_note_idx_ = note_idx;
  folder_overview_mode_ = false;
  load_note_content_for_active();
  save_index();
}

void App::sync_active_note_meta()
{
  if(folders_.empty()) return;
  FolderMeta &f = folders_[(size_t)active_folder_idx_];
  if(f.notes.empty()) return;
  NoteMeta &n = f.notes[(size_t)active_note_idx_];
  n.title = note_title_;
  n.path = state_file_path_;
  save_index();
}

void App::rename_note_storage_for_title(const std::string &new_title)
{
  const std::string safe_title = sanitize_note_filename(new_title);
  const std::string folder_name = folders_.empty() ? "General" : folders_[(size_t)active_folder_idx_].name;
  std::filesystem::path new_path = make_note_path(folder_name, safe_title);

  if(new_path.string() == state_file_path_)
  {
    note_title_ = safe_title;
    sync_active_note_meta();
    return;
  }

  std::error_code ec;
  std::filesystem::path current_path(state_file_path_);
  if(std::filesystem::exists(current_path, ec))
  {
    if(std::filesystem::exists(new_path, ec))
      std::filesystem::remove(new_path, ec);
    std::filesystem::rename(current_path, new_path, ec);
  }

  state_file_path_ = new_path.string();
  note_title_ = safe_title;
  sync_active_note_meta();
  save_state();
}

void App::rename_note_by_index(int folder_idx, int note_idx, const std::string &new_title)
{
  ensure_default_index();
  folder_idx = std::max(0, std::min(folder_idx, (int)folders_.size() - 1));
  note_idx = std::max(0, std::min(note_idx, (int)folders_[(size_t)folder_idx].notes.size() - 1));

  const std::string safe_title = sanitize_note_filename(new_title);
  FolderMeta &f = folders_[(size_t)folder_idx];
  NoteMeta &n = f.notes[(size_t)note_idx];
  std::filesystem::path new_path = make_note_path(f.name, safe_title);

  if(new_path.string() != n.path)
  {
    std::error_code ec;
    std::filesystem::path old_path(n.path);
    if(std::filesystem::exists(old_path, ec))
    {
      if(std::filesystem::exists(new_path, ec))
        std::filesystem::remove(new_path, ec);
      std::filesystem::rename(old_path, new_path, ec);
    }
    n.path = new_path.string();
  }

  n.title = safe_title;
  if(folder_idx == active_folder_idx_ && note_idx == active_note_idx_)
  {
    state_file_path_ = n.path;
    note_title_ = n.title;
  }
  save_index();
}

void App::push_undo_snapshot()
{
  push_undo_snapshot_from(markdown_text_);
}

void App::push_undo_snapshot_from(const std::string &snapshot)
{
  if(!undo_stack_.empty() && undo_stack_.back() == snapshot) return;
  if(undo_stack_.size() >= 64) undo_stack_.erase(undo_stack_.begin());
  undo_stack_.push_back(snapshot);
  redo_stack_.clear();
}

void App::apply_undo_snapshot()
{
  if(undo_stack_.empty()) return;
  if(redo_stack_.size() >= 64) redo_stack_.erase(redo_stack_.begin());
  redo_stack_.push_back(markdown_text_);
  markdown_text_ = undo_stack_.back();
  undo_stack_.pop_back();
  normalize_input_text_buffer(markdown_text_);
  save_state();
}

void App::apply_redo_snapshot()
{
  if(redo_stack_.empty()) return;
  if(undo_stack_.size() >= 64) undo_stack_.erase(undo_stack_.begin());
  undo_stack_.push_back(markdown_text_);
  markdown_text_ = redo_stack_.back();
  redo_stack_.pop_back();
  normalize_input_text_buffer(markdown_text_);
  save_state();
}

void App::frame_begin()
{
  SDL_Event event;
  while(SDL_PollEvent(&event))
  {
    if(event.type == SDL_QUIT) running_ = false;
    if(event.type == SDL_WINDOWEVENT &&
       event.window.event == SDL_WINDOWEVENT_CLOSE &&
       event.window.windowID == SDL_GetWindowID(window_))
    {
      running_ = false;
    }

    // While editing, keep Esc out of InputText so it doesn't cancel/revert the latest edit.
    if(editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_ESCAPE)
    {
      request_exit_edit_mode_ = true;
      continue;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
}
void App::frame_ui()
{
  // --- Dock host ---
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoDocking;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##DockHost", nullptr, host_flags);
  ImGui::PopStyleVar(2);

  ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();

  // --- Explorer window: static left sidebar ---
  const float explorer_w = 280.0f;
  ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(explorer_w, vp->Size.y), ImGuiCond_Always);
  ImGui::Begin(
      "Explorer",
      nullptr,
      ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking);
  static char new_folder_buf[128] = {};
  static char new_note_buf[128] = {};
  static bool open_new_folder_popup = false;
  static bool open_new_note_popup = false;
  static int new_note_target_folder_idx = -1;
  static int force_open_folder_idx = -1;
  static bool open_rename_note_popup = false;
  static int rename_note_folder_idx = -1;
  static int rename_note_idx = -1;
  static char rename_note_buf[256] = {};
  static int pending_delete_folder_idx = -1;
  static int pending_delete_note_folder_idx = -1;
  static int pending_delete_note_idx = -1;

  if(ImGui::Button("+ Folder")) open_new_folder_popup = true;
  ImGui::SameLine();
  if(ImGui::Button("+ Note"))
  {
    open_new_note_popup = true;
    new_note_target_folder_idx = active_folder_idx_;
  }

  if(open_new_folder_popup)
  {
    ImGui::OpenPopup("New Folder");
    open_new_folder_popup = false;
    std::snprintf(new_folder_buf, sizeof(new_folder_buf), "Folder");
  }
  if(open_new_note_popup)
  {
    ImGui::OpenPopup("New Note");
    open_new_note_popup = false;
    std::snprintf(new_note_buf, sizeof(new_note_buf), "Note");
  }
  if(open_rename_note_popup)
  {
    ImGui::OpenPopup("Rename Note Sidebar");
    open_rename_note_popup = false;
  }

  if(ImGui::BeginPopup("New Folder"))
  {
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Name", new_folder_buf, sizeof(new_folder_buf));
    if(ImGui::Button("Create"))
    {
      FolderMeta f;
      f.name = sanitize_note_filename(new_folder_buf);
      NoteMeta n;
      n.title = "Note";
      n.path = make_note_path(f.name, n.title);
      f.notes.push_back(std::move(n));
      folders_.push_back(std::move(f));
      save_state();
      active_folder_idx_ = (int)folders_.size() - 1;
      active_note_idx_ = 0;
      folder_overview_mode_ = true;
      load_note_content_for_active();
      save_index();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("New Note"))
  {
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Title", new_note_buf, sizeof(new_note_buf));
    if(ImGui::Button("Create"))
    {
      ensure_default_index();
      const int target_folder_idx =
          (new_note_target_folder_idx >= 0 && new_note_target_folder_idx < (int)folders_.size())
              ? new_note_target_folder_idx
              : active_folder_idx_;
      FolderMeta &f = folders_[(size_t)target_folder_idx];
      NoteMeta n;
      n.title = sanitize_note_filename(new_note_buf);
      n.path = make_note_path(f.name, n.title);
      f.notes.push_back(std::move(n));
      active_folder_idx_ = target_folder_idx;
      active_note_idx_ = (int)f.notes.size() - 1;
      force_open_folder_idx = target_folder_idx;
      save_index();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Rename Note Sidebar"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText("Nou nom", rename_note_buf, sizeof(rename_note_buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      if(rename_note_folder_idx >= 0 && rename_note_idx >= 0)
      {
        rename_note_by_index(rename_note_folder_idx, rename_note_idx, rename_note_buf);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopupContextWindow("ExplorerContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
  {
    if(ImGui::MenuItem("Nova carpeta"))
    {
      open_new_folder_popup = true;
    }
    if(ImGui::MenuItem("Nova nota"))
    {
      open_new_note_popup = true;
      new_note_target_folder_idx = active_folder_idx_;
    }
    ImGui::EndPopup();
  }

  folder_overview_mode_ = true;
  ensure_default_index();
  for(int fi = 0; fi < (int)folders_.size(); ++fi)
  {
    FolderMeta &f = folders_[(size_t)fi];
    if(fi == force_open_folder_idx) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    ImGuiTreeNodeFlags ff = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if(fi == active_folder_idx_ && folder_overview_mode_) ff |= ImGuiTreeNodeFlags_Selected;
    bool open = ImGui::TreeNodeEx((void *)(intptr_t)(fi + 1), ff, "%s", f.name.c_str());
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
      save_state();
      active_folder_idx_ = fi;
      folder_overview_mode_ = true;
      editing_mode_ = false;
      request_exit_edit_mode_ = false;
      save_index();
    }

    const std::string folder_popup_id = "FolderCtx##" + std::to_string(fi);
    if(ImGui::BeginPopupContextItem(folder_popup_id.c_str(), ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem("Nova nota a la carpeta"))
      {
        new_note_target_folder_idx = fi;
        open_new_note_popup = true;
      }
      if(ImGui::MenuItem("Nova carpeta"))
      {
        open_new_folder_popup = true;
      }
      if(ImGui::MenuItem("Elimina carpeta"))
      {
        pending_delete_folder_idx = fi;
      }
      ImGui::EndPopup();
    }

    if(open)
    {
      for(int ni = 0; ni < (int)f.notes.size(); ++ni)
      {
        NoteMeta &n = f.notes[(size_t)ni];
        const bool note_selected = (fi == active_folder_idx_ && ni == active_note_idx_);
        if(ImGui::Selectable(n.title.c_str(), note_selected))
        {
          save_state();
          active_folder_idx_ = fi;
          active_note_idx_ = ni;
          force_open_folder_idx = fi;
          editing_mode_ = false;
          request_exit_edit_mode_ = false;
          load_note_content_for_active();
          save_index();
        }

        const std::string note_popup_id = "NoteCtx##" + std::to_string(fi) + "_" + std::to_string(ni);
        if(ImGui::BeginPopupContextItem(note_popup_id.c_str(), ImGuiPopupFlags_MouseButtonRight))
        {
          if(ImGui::MenuItem("Canvia nom"))
          {
            rename_note_folder_idx = fi;
            rename_note_idx = ni;
            std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s", n.title.c_str());
            open_rename_note_popup = true;
          }
          if(ImGui::MenuItem("Edita aquesta nota"))
          {
            save_state();
            active_folder_idx_ = fi;
            active_note_idx_ = ni;
            load_note_content_for_active();
            editing_mode_ = true;
            request_exit_edit_mode_ = false;
            force_open_folder_idx = fi;
            save_index();
          }
          if(ImGui::MenuItem("Nova nota a la carpeta"))
          {
            new_note_target_folder_idx = fi;
            open_new_note_popup = true;
          }
          if(ImGui::MenuItem("Elimina nota"))
          {
            pending_delete_note_folder_idx = fi;
            pending_delete_note_idx = ni;
          }
          ImGui::EndPopup();
        }
      }
      ImGui::TreePop();
    }
  }
  if(pending_delete_note_folder_idx >= 0 && pending_delete_note_idx >= 0)
  {
    save_state();
    const int fi = pending_delete_note_folder_idx;
    const int ni = pending_delete_note_idx;
    if(fi >= 0 && fi < (int)folders_.size())
    {
      FolderMeta &df = folders_[(size_t)fi];
      if(ni >= 0 && ni < (int)df.notes.size())
      {
        std::error_code ec;
        std::filesystem::remove(df.notes[(size_t)ni].path, ec);
        df.notes.erase(df.notes.begin() + ni);
      }
      if(df.notes.empty())
      {
        folders_.erase(folders_.begin() + fi);
      }
    }
    ensure_default_index();
    active_folder_idx_ = std::max(0, std::min(active_folder_idx_, (int)folders_.size() - 1));
    active_note_idx_ = std::max(0, std::min(active_note_idx_, (int)folders_[(size_t)active_folder_idx_].notes.size() - 1));
    load_note_content_for_active();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    save_index();
    pending_delete_note_folder_idx = -1;
    pending_delete_note_idx = -1;
  }
  if(pending_delete_folder_idx >= 0)
  {
    save_state();
    const int fi = pending_delete_folder_idx;
    if(fi >= 0 && fi < (int)folders_.size())
    {
      FolderMeta &df = folders_[(size_t)fi];
      for(const NoteMeta &n : df.notes)
      {
        std::error_code ec;
        std::filesystem::remove(n.path, ec);
      }
      folders_.erase(folders_.begin() + fi);
    }
    ensure_default_index();
    active_folder_idx_ = std::max(0, std::min(active_folder_idx_, (int)folders_.size() - 1));
    active_note_idx_ = std::max(0, std::min(active_note_idx_, (int)folders_[(size_t)active_folder_idx_].notes.size() - 1));
    load_note_content_for_active();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    save_index();
    pending_delete_folder_idx = -1;
  }
  force_open_folder_idx = -1;
  ImGui::End();

  auto read_file_text = [](const std::string &path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    if(!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  };

  if(folder_overview_mode_)
  {
    static bool refocus_folder_editor = false;
    static int rename_win_folder_idx = -1;
    static int rename_win_note_idx = -1;
    static char rename_win_buf[256] = {};
    static int anchor_sel_start = 0;
    static int anchor_sel_end = 0;
    static MdFormatState fmt_folder;
    ensure_default_index();
    active_folder_idx_ = std::max(0, std::min(active_folder_idx_, (int)folders_.size() - 1));
    FolderMeta &f = folders_[(size_t)active_folder_idx_];

    if(editing_mode_)
    {
      const bool has_anchor_selection = (anchor_sel_start != anchor_sel_end);
      const float bar_h = 44.0f;
      ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + explorer_w, vp->Pos.y), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(std::max(200.0f, vp->Size.x - explorer_w), bar_h), ImGuiCond_Always);
      ImGui::Begin(
          "##FormatTopBar",
          nullptr,
          ImGuiWindowFlags_NoTitleBar |
              ImGuiWindowFlags_NoMove |
              ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoSavedSettings |
              ImGuiWindowFlags_NoDocking);

      ImGui::BeginDisabled(!has_anchor_selection);
      if(ImGui::Button("Italic"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
        normalize_input_text_buffer(markdown_text_);
        save_state();
      }
      ImGui::SameLine();
      if(ImGui::Button("Bold"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
        normalize_input_text_buffer(markdown_text_);
        save_state();
      }
      ImGui::SameLine();
      if(ImGui::Button("Strike"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
        normalize_input_text_buffer(markdown_text_);
        save_state();
      }
      ImGui::SameLine();
      if(ImGui::Button("Note"))
      {
        push_undo_snapshot();
        apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
        normalize_input_text_buffer(markdown_text_);
        save_state();
      }
      ImGui::SameLine();
      ImGui::ColorEdit3("##top_color", (float *)&fmt_folder.color, ImGuiColorEditFlags_NoInputs);
      ImGui::SameLine();
      if(ImGui::Button("Color Apply"))
      {
        push_undo_snapshot();
        const std::string hex = rgba_to_hex(fmt_folder.color);
        apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, hex);
        normalize_input_text_buffer(markdown_text_);
        save_state();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if(ImGui::Button("Task List"))
      {
        push_undo_snapshot();
        insert_checklist_item_at_cursor(markdown_text_, fmt_folder);
        normalize_input_text_buffer(markdown_text_);
        save_state();
      }
      ImGui::End();
    }

    for(int ni = 0; ni < (int)f.notes.size(); ++ni)
    {
      NoteMeta &n = f.notes[(size_t)ni];
      const std::string window_id = n.title + "###FolderNote_" + std::to_string(active_folder_idx_) + "_" + std::to_string(ni);

      if(n.has_layout)
      {
        ImGui::SetNextWindowPos(ImVec2(n.pos_x, n.pos_y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, n.width), std::max(140.0f, n.height)), ImGuiCond_FirstUseEver);
      }
      else
      {
        const ImVec2 base(340.0f + 40.0f * (float)(ni % 3), 180.0f + 40.0f * (float)(ni % 3));
        ImGui::SetNextWindowPos(base, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 260.0f), ImGuiCond_FirstUseEver);
      }

      ImGui::Begin(
          window_id.c_str(),
          nullptr,
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

      const ImVec2 win_pos = ImGui::GetWindowPos();
      const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
      const ImVec2 mouse_pos = ImGui::GetMousePos();
      const bool mouse_on_title =
          mouse_pos.x >= win_pos.x &&
          mouse_pos.x <= (win_pos.x + ImGui::GetWindowWidth()) &&
          mouse_pos.y >= win_pos.y &&
          mouse_pos.y <= (win_pos.y + title_bar_h);
      if(mouse_on_title && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        rename_win_folder_idx = active_folder_idx_;
        rename_win_note_idx = ni;
        std::snprintf(rename_win_buf, sizeof(rename_win_buf), "%s", n.title.c_str());
        ImGui::OpenPopup("Rename Note Window");
      }
      if(ImGui::BeginPopup("Rename Note Window"))
      {
        ImGui::SetNextItemWidth(240.0f);
        if(ImGui::InputText("Nou nom", rename_win_buf, sizeof(rename_win_buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
          if(rename_win_folder_idx >= 0 && rename_win_note_idx >= 0)
          {
            rename_note_by_index(rename_win_folder_idx, rename_win_note_idx, rename_win_buf);
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      const bool is_editing_this = editing_mode_ && ni == active_note_idx_;
      bool changed = false;

      if(is_editing_this)
      {
        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_AllowTabInput |
            ImGuiInputTextFlags_CallbackResize |
            ImGuiInputTextFlags_CallbackEdit |
            ImGuiInputTextFlags_CallbackAlways;
        static MdEditorUserData ud_folder{&markdown_text_, &fmt_folder};
        ud_folder.text = &markdown_text_;

        if(refocus_folder_editor)
        {
          ImGui::SetKeyboardFocusHere();
          refocus_folder_editor = false;
        }

        const std::string before_edit = markdown_text_;
        changed = ImGui::InputTextMultiline(
            "##md_folder",
            markdown_text_.data(),
            markdown_text_.capacity() + 1,
            ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
            flags,
            [](ImGuiInputTextCallbackData *data) -> int {
              auto *ud = static_cast<MdEditorUserData *>(data->UserData);
              if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
              {
                ud->text->resize((size_t)data->BufTextLen);
                data->Buf = ud->text->data();
                return 0;
              }
              data->UserData = ud->fmt;
              return md_editor_cb(data);
            },
            &ud_folder);
        normalize_input_text_buffer(markdown_text_);
        if(changed)
        {
          push_undo_snapshot_from(before_edit);
          save_state();
        }

        const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const int a = fmt_folder.sel_start;
        const int b = fmt_folder.sel_end;
        const bool has_selection = (a != b);
        const int sel_min = (a < b) ? a : b;
        const int sel_max = (a < b) ? b : a;
        if(has_selection)
        {
          anchor_sel_start = sel_min;
          anchor_sel_end = sel_max;
        }
        (void)editor_hovered;
      }
      else
      {
        std::string preview_text = read_file_text(n.path);
        const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f);
        MarkdownView::set_render_width(preview_w);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        changed = render_preview_with_task_checkboxes(preview_text);
        ImGui::PopTextWrapPos();
        if(changed)
        {
          std::ofstream out(n.path, std::ios::binary | std::ios::trunc);
          if(out) out << preview_text;
        }
      }

      const bool w_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      if(w_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
        if(editing_mode_ && !is_editing_this)
        {
          normalize_input_text_buffer(markdown_text_);
          save_state();
          editing_mode_ = false;
        }
        if(!is_editing_this)
        {
          active_note_idx_ = ni;
          load_note_content_for_active();
          editing_mode_ = true;
          request_exit_edit_mode_ = false;
          refocus_folder_editor = true;
          save_index();
        }
      }

      const float auto_h = std::max(140.0f, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
      const ImVec2 pos = ImGui::GetWindowPos();
      const ImVec2 size = ImGui::GetWindowSize();

      if(!is_editing_this)
      {
        ImGui::SetWindowSize(ImVec2(size.x, auto_h));
      }

      auto changed_f = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
      if(changed_f(n.pos_x, pos.x) || changed_f(n.pos_y, pos.y) || changed_f(n.width, size.x) || changed_f(n.height, auto_h) || !n.has_layout)
      {
        n.pos_x = pos.x;
        n.pos_y = pos.y;
        n.width = size.x;
        n.height = auto_h;
        n.has_layout = true;
        layout_dirty_ = true;
      }

      ImGui::End();
    }

    if(editing_mode_ && request_exit_edit_mode_)
    {
      normalize_input_text_buffer(markdown_text_);
      save_state();
      editing_mode_ = false;
      request_exit_edit_mode_ = false;
    }

    if(layout_dirty_ && !ImGui::IsAnyMouseDown())
    {
      save_index();
      layout_dirty_ = false;
    }
    return;
  }

  // --- Single window: "Note" (preview + edit overlay) ---
  ensure_default_index();
  NoteMeta &active_note = folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_];
  static float note_window_height = 360.0f;
  auto compute_edit_window_height = [&]() -> float {
    const ImGuiStyle &st = ImGui::GetStyle();
    const float title_bar_h = ImGui::GetFontSize() + st.FramePadding.y * 2.0f;
    const int line_count = 1 + (int)std::count(markdown_text_.begin(), markdown_text_.end(), '\n');
    const float text_h = line_count * ImGui::GetTextLineHeightWithSpacing();
    const float input_h = text_h + st.FramePadding.y * 2.0f + 10.0f;
    return std::max(140.0f, title_bar_h + st.WindowPadding.y * 2.0f + input_h);
  };

  if(editing_mode_) note_window_height = compute_edit_window_height();

  if(active_note.has_layout)
  {
    ImGui::SetNextWindowPos(ImVec2(active_note.pos_x, active_note.pos_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, active_note.width), std::max(140.0f, active_note.height)), ImGuiCond_FirstUseEver);
  }
  else
  {
    ImGui::SetNextWindowSize(ImVec2(520.0f, note_window_height), ImGuiCond_FirstUseEver);
  }

  ImGui::SetNextWindowSizeConstraints(
      ImVec2(320.0f, note_window_height),
      ImVec2(FLT_MAX, note_window_height));

  std::string note_window_label = note_title_ + "###NoteWindow";
  ImGui::Begin(
      note_window_label.c_str(),
      nullptr,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

  // Right click on title bar to rename the note window title.
  static bool open_rename_popup = false;
  static char rename_buf[256] = {};
  const ImVec2 win_pos = ImGui::GetWindowPos();
  const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
  const ImVec2 mouse_pos = ImGui::GetMousePos();
  const bool mouse_on_title =
      mouse_pos.x >= win_pos.x &&
      mouse_pos.x <= (win_pos.x + ImGui::GetWindowWidth()) &&
      mouse_pos.y >= win_pos.y &&
      mouse_pos.y <= (win_pos.y + title_bar_h);
  if(mouse_on_title && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
  {
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", note_title_.c_str());
    open_rename_popup = true;
  }
  if(open_rename_popup)
  {
    ImGui::OpenPopup("Rename Note");
    open_rename_popup = false;
  }
  if(ImGui::BeginPopup("Rename Note"))
  {
    ImGui::TextUnformatted("Nom de la finestra:");
    ImGui::SetNextItemWidth(260.0f);
    if(ImGui::InputText("##rename_note_title", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      normalize_input_text_buffer(markdown_text_);
      rename_note_storage_for_title(rename_buf);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  static bool show_palette = false;
  static bool refocus_editor = false;
  static MdFormatState fmt;
  static MdEditorUserData ud{&markdown_text_, &fmt};
  ud.text = &markdown_text_;

  if(!editing_mode_)
  {
    // Preview mode (interactive)
    const float start_y = ImGui::GetCursorPosY();
    const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f);
    MarkdownView::set_render_width(preview_w);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    const bool task_changed = render_preview_with_task_checkboxes(markdown_text_);
    ImGui::PopTextWrapPos();
    if(task_changed) save_state();
    (void)start_y;

    // Enter edit mode only on double click (single click does nothing)
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      editing_mode_ = true;
      show_palette = false;
      // Next frame, focus the editor widget
      refocus_editor = true;
    }
  }
  else
  {
    // Plain text editor mode
    ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput |
        ImGuiInputTextFlags_CallbackResize |
        ImGuiInputTextFlags_CallbackEdit |
        ImGuiInputTextFlags_CallbackAlways;
    if(refocus_editor)
    {
      ImGui::SetKeyboardFocusHere();
      refocus_editor = false;
    }

    const std::string before_edit = markdown_text_;
    const bool text_changed = ImGui::InputTextMultiline(
        "##md",
        markdown_text_.data(),
        markdown_text_.capacity() + 1,
        ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
        flags,
        [](ImGuiInputTextCallbackData *data) -> int {
          auto *ud = static_cast<MdEditorUserData *>(data->UserData);
          if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
          {
            ud->text->resize((size_t)data->BufTextLen);
            data->Buf = ud->text->data();
            return 0;
          }
          data->UserData = ud->fmt; // md_editor_cb expects MdFormatState*
          return md_editor_cb(data);
        },
        &ud);
    normalize_input_text_buffer(markdown_text_);
    if(text_changed)
    {
      push_undo_snapshot_from(before_edit);
      save_state();
    }

    // After the widget: show popup if selection is non-empty and editor is focused/active
    const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    const int a = fmt.sel_start, b = fmt.sel_end;
    const bool has_selection = (a != b);
    const int sel_min = (a < b) ? a : b;
    const int sel_max = (a < b) ? b : a;
    static int anchor_sel_start = 0;
    static int anchor_sel_end = 0;

    // ---- Clickable floating formatting palette (tooltip-like) ----
    static ImVec2 palette_pos(0, 0);
    static bool palette_just_opened = false;

    if(has_selection)
    {
      anchor_sel_start = sel_min;
      anchor_sel_end = sel_max;
    }

    const bool has_anchor_selection = (anchor_sel_start != anchor_sel_end);

    // Open palette only with right-click while editing and text is selected.
    if(editor_hovered &&
       (has_selection || has_anchor_selection) &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      palette_pos = ImGui::GetMousePos();
      show_palette = true;
      palette_just_opened = true;
    }
    bool palette_hovered = false;
    if(show_palette && has_anchor_selection)
    {
      ImGui::SetNextWindowPos(palette_pos, ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.95f);
      if(palette_just_opened) ImGui::SetNextWindowFocus();

      ImGuiWindowFlags pal_flags =
          ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNavFocus |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking; // NOTE: no Tooltip flag (Tooltip => NoInputs)

      ImGui::Begin("##md_format_palette", nullptr, pal_flags);
      const bool pal_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      palette_hovered = pal_hovered;

      bool applied = false;

      if(ImGui::Button("Italic"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Bold"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Strike"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Note"))
      {
        push_undo_snapshot();
        apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
        applied = true;
      }

      ImGui::Separator();

      ImGui::ColorEdit3("Color", (float *)&fmt.color, ImGuiColorEditFlags_NoInputs);
      ImGui::SameLine();
      if(ImGui::Button("Apply"))
      {
        push_undo_snapshot();
        const std::string hex = rgba_to_hex(fmt.color);
        apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, hex);
        applied = true;
      }

      ImGui::End();

      if(applied)
      {
        normalize_input_text_buffer(markdown_text_);
        save_state();
        fmt.sel_start = anchor_sel_start;
        fmt.sel_end = anchor_sel_end;
      }
    }

    // Hide when clicking outside tooltip window (or if selection is gone).
    const bool any_popup_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool clicked_outside_palette =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
        !palette_hovered &&
        !any_popup_open &&
        !palette_just_opened;
    if(show_palette && (!has_anchor_selection || clicked_outside_palette))
    {
      show_palette = false;
      anchor_sel_start = 0;
      anchor_sel_end = 0;
    }
    palette_just_opened = false;
  }

  // Exit edit mode only with Esc.
  if(editing_mode_ && request_exit_edit_mode_)
  {
    normalize_input_text_buffer(markdown_text_);
    save_state();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    show_palette = false;
    refocus_editor = false;
  }

  if(editing_mode_)
    note_window_height = compute_edit_window_height();
  else
    note_window_height = std::max(140.0f, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);

  {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    auto changed_f = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
    if(changed_f(active_note.pos_x, pos.x) || changed_f(active_note.pos_y, pos.y) ||
       changed_f(active_note.width, size.x) || changed_f(active_note.height, note_window_height) ||
       !active_note.has_layout)
    {
      active_note.pos_x = pos.x;
      active_note.pos_y = pos.y;
      active_note.width = size.x;
      active_note.height = note_window_height;
      active_note.has_layout = true;
      layout_dirty_ = true;
    }
  }

  ImGui::End();

  if(layout_dirty_ && !ImGui::IsAnyMouseDown())
  {
    save_index();
    layout_dirty_ = false;
  }
}

void App::frame_end()
{
  ImGui::Render();

  int display_w = 0, display_h = 0;
  SDL_GL_GetDrawableSize(window_, &display_w, &display_h);

  glViewport(0, 0, display_w, display_h);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(window_);
}
