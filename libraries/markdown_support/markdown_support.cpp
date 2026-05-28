#include "markdown_support.hpp"

#include "markdown_sections.hpp"
#include "markdown_view.hpp"
#include "markdown_ui.hpp"
#include "mermaid_flowchart.hpp"
#include "mermaid_diagrams.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace MarkdownSupport
{
PreviewRenderResult render_preview_with_task_checkboxes_ex(std::string &markdown);
void set_preview_document_path(std::string_view path);

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
  return std::isalnum(uc) != 0;
}

std::pair<int, int> expand_word_bounds(const std::string &text, int pos)
{
  const int n = static_cast<int>(text.size());
  if(pos < 0 || pos >= n) return {std::max(0, std::min(pos, n)), std::max(0, std::min(pos, n))};
  if(!is_word_char(text[static_cast<size_t>(pos)])) return {pos, pos};

  int start = pos;
  while(start > 0 && is_word_char(text[static_cast<size_t>(start - 1)])) --start;

  int end = pos + 1;
  while(end < n && is_word_char(text[static_cast<size_t>(end)])) ++end;

  return {start, end};
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
         t == "sankey-beta" || t == "sankey" ||
         t == "xychart-beta" || t == "xychart" ||
         t == "block-beta" || t == "block" ||
         t == "packet-beta" || t == "packet" ||
         t == "kanban" ||
         t == "architecture-beta" || t == "architecture" ||
         t == "radar-beta" || t == "radar" ||
         t == "treemap-beta" || t == "treemap" ||
         t == "eventmodeling" ||
         t == "venn" ||
         t == "ishikawa" ||
         t == "wardley" ||
         t == "treeview";
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

  // ── already-rendered types ──────────────────────────────────────────────
  if(mt == "pie")
  {
    MermaidPieChart pie;
    if(parse_mermaid_pie(body, pie))   render_mermaid_pie_chart(pie, id);
    else                               render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }
  if(mt == "flowchart" || mt == "graph")
  {
    MermaidFlowchart::Graph g;
    if(MermaidFlowchart::parse(body, g)) MermaidFlowchart::render(g, id);
    else                                 render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }

// helper macro: parse + render or fall back to placeholder
#define MERMAID_DISPATCH(parse_fn, render_fn, DiagramType) \
  { MermaidDiagrams::DiagramType d; \
    if(MermaidDiagrams::parse_fn(body, d)) MermaidDiagrams::render_fn(d, id); \
    else render_mermaid_placeholder(mermaid_type, body, id); return; }

  if(mt == "sequencediagram")                      MERMAID_DISPATCH(parse_sequence,     render_sequence,     SequenceDiagram)
  if(mt == "classdiagram")                         MERMAID_DISPATCH(parse_class,        render_class,        ClassDiagram)
  if(mt == "statediagram" || mt == "statediagram-v2") MERMAID_DISPATCH(parse_state,     render_state,        StateDiagram)
  if(mt == "erdiagram")                            MERMAID_DISPATCH(parse_er,           render_er,           ERDiagram)
  if(mt == "journey")                              MERMAID_DISPATCH(parse_journey,      render_journey,      JourneyDiagram)
  if(mt == "gantt")                                MERMAID_DISPATCH(parse_gantt,        render_gantt,        GanttDiagram)
  if(mt == "quadrantchart")                        MERMAID_DISPATCH(parse_quadrant,     render_quadrant,     QuadrantDiagram)
  if(mt == "requirementdiagram")                   MERMAID_DISPATCH(parse_requirement,  render_requirement,  RequirementDiagram)
  if(mt == "gitgraph")                             MERMAID_DISPATCH(parse_git,          render_git,          GitDiagram)
  if(mt == "mindmap")                              MERMAID_DISPATCH(parse_mindmap,      render_mindmap,      MindmapDiagram)
  if(mt == "timeline")                             MERMAID_DISPATCH(parse_timeline,     render_timeline,     TimelineDiagram)
  if(mt == "sankey-beta" || mt == "sankey")        MERMAID_DISPATCH(parse_sankey,       render_sankey,       SankeyDiagram)
  if(mt == "xychart-beta" || mt == "xychart")      MERMAID_DISPATCH(parse_xychart,      render_xychart,      XYDiagram)
  if(mt == "block-beta"   || mt == "block")        MERMAID_DISPATCH(parse_block,        render_block,        BlockDiagram)
  if(mt == "packet-beta"  || mt == "packet")       MERMAID_DISPATCH(parse_packet,       render_packet,       PacketDiagram)
  if(mt == "kanban")                               MERMAID_DISPATCH(parse_kanban,       render_kanban,       KanbanDiagram)
  if(mt == "architecture-beta" || mt == "architecture") MERMAID_DISPATCH(parse_architecture, render_architecture, ArchDiagram)
  if(mt == "radar-beta"   || mt == "radar")        MERMAID_DISPATCH(parse_radar,        render_radar,        RadarDiagram)
  if(mt == "treemap-beta" || mt == "treemap")      MERMAID_DISPATCH(parse_treemap,      render_treemap,      TreemapDiagram)
  if(mt == "eventmodeling")                        MERMAID_DISPATCH(parse_eventmodeling,render_eventmodeling,EventModelingDiagram)
  if(mt == "venn")                                 MERMAID_DISPATCH(parse_venn,         render_venn,         VennDiagram)
  if(mt == "ishikawa")                             MERMAID_DISPATCH(parse_ishikawa,     render_ishikawa,     IshikawaDiagram)
  if(mt == "wardley")                              MERMAID_DISPATCH(parse_wardley,      render_wardley,      WardleyDiagram)
  if(mt == "treeview")                             MERMAID_DISPATCH(parse_treeview,     render_treeview,     TreeViewDiagram)
  if(mt == "zenuml")
  {
    MermaidDiagrams::SequenceDiagram d;
    if(MermaidDiagrams::parse_zenuml(body, d)) MermaidDiagrams::render_zenuml(d, id);
    else render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }
#undef MERMAID_DISPATCH

  render_mermaid_placeholder(mermaid_type, body, id);
}

using Json = nlohmann::json;

constexpr const char *kMarkdownPreviewStateFile = DATA_PATH "/markdown_preview_state.json";

struct TableViewState
{
  int sort_column = -1;
  bool sort_ascending = true;
  std::string contains_filter;
  int contains_filter_column = -1;
  std::string not_contains_filter;
  int not_contains_filter_column = -1;
};

struct ParsedMarkdownTable
{
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;
  size_t block_start = 0;
  size_t block_end = 0;
  bool trailing_newline = false;
};

struct TableReplacement
{
  size_t start = 0;
  size_t end = 0;
  std::string replacement;
};

struct TableRenderOutcome
{
  bool markdown_changed = false;
  bool preview_state_changed = false;
  bool consumed_right_click = false;
  bool consumed_double_click = false;
  bool has_replacement = false;
  std::string replacement;
};

struct CellEditorState
{
  bool active = false;
  bool request_focus = false;
  bool was_active_last_frame = false;
  std::string document_key;
  int table_id = -1;
  bool header = false;
  int raw_row = -1;
  int column = -1;
  char buffer[512] = {};
};

struct FilterDialogState
{
  bool active = false;
  bool open_request = false;
  bool request_focus = false;
  bool not_contains = false;
  std::string document_key;
  int table_id = -1;
  int column = -1;
  std::string popup_id;
  char buffer[256] = {};
};

std::string g_preview_document_path;
Json g_preview_state_json;
bool g_preview_state_loaded = false;
bool g_preview_state_dirty = false;
std::unordered_map<std::string, std::unordered_map<int, TableViewState>> g_table_state_cache;
CellEditorState g_cell_editor_state;
FilterDialogState g_filter_dialog_state;
bool g_rendering_hover_preview = false;
bool g_force_open_preview_headers = false;
int g_hover_preview_drawn_frame = -1;

void open_table_cell_editor(
    const std::string &document_key,
    int table_id,
    bool header_cell,
    int raw_row,
    int column,
    const std::string &value)
{
  g_cell_editor_state.active = true;
  g_cell_editor_state.request_focus = true;
  g_cell_editor_state.was_active_last_frame = false;
  g_cell_editor_state.document_key = document_key;
  g_cell_editor_state.table_id = table_id;
  g_cell_editor_state.header = header_cell;
  g_cell_editor_state.raw_row = raw_row;
  g_cell_editor_state.column = column;
  std::snprintf(g_cell_editor_state.buffer, sizeof(g_cell_editor_state.buffer), "%s", value.c_str());
}

void render_link_hover_preview_popup()
{
  if(g_rendering_hover_preview) return;

  const int frame = ImGui::GetFrameCount();
  if(g_hover_preview_drawn_frame == frame) return;

  MarkdownHoverPreviewData preview;
  if(!MarkdownView::take_hover_preview(preview)) return;

  g_hover_preview_drawn_frame = frame;
  g_rendering_hover_preview = true;
  const std::string previous_document_path = g_preview_document_path;
  std::string preview_markdown = preview.body;

  ImGui::SetNextWindowPos(ImVec2(preview.mouse_pos.x + 18.0f, preview.mouse_pos.y + 18.0f), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 140.0f), ImVec2(560.0f, 440.0f));

  MarkdownView::set_hover_preview_enabled(false);
  set_preview_document_path(preview.path);
  g_force_open_preview_headers = true;

  const std::string window_title = preview.title + "##link_preview";
  bool window_hovered = false;
  if(ImGui::Begin(window_title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
  {
    window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    (void)render_preview_with_task_checkboxes_ex(preview_markdown);
  }
  ImGui::End();

  if(!preview.link_hovered && !window_hovered) MarkdownView::clear_hover_preview();

  g_force_open_preview_headers = false;
  set_preview_document_path(previous_document_path);
  MarkdownView::set_hover_preview_enabled(true);
  g_rendering_hover_preview = false;
}

std::string current_document_key()
{
  return g_preview_document_path.empty() ? std::string("__active_note__") : g_preview_document_path;
}

void ensure_preview_state_loaded()
{
  if(g_preview_state_loaded) return;
  g_preview_state_loaded = true;

  g_preview_state_json = Json::object();
  std::ifstream in(kMarkdownPreviewStateFile, std::ios::binary);
  if(in)
  {
    try
    {
      in >> g_preview_state_json;
    }
    catch(...)
    {
      g_preview_state_json = Json::object();
    }
  }

  if(!g_preview_state_json.is_object()) g_preview_state_json = Json::object();
  if(!g_preview_state_json.contains("documents") || !g_preview_state_json["documents"].is_object())
    g_preview_state_json["documents"] = Json::object();
}

std::string first_non_empty_filter(const Json &arr)
{
  if(!arr.is_array()) return {};
  for(const Json &v : arr)
  {
    if(!v.is_string()) continue;
    const std::string s = v.get<std::string>();
    if(!NoteCore::trim(s).empty()) return s;
  }
  return {};
}

TableViewState parse_table_state_from_json(const Json &obj)
{
  TableViewState st;
  if(!obj.is_object()) return st;

  st.sort_column = obj.value("sort_column", -1);
  st.sort_ascending = obj.value("sort_ascending", true);

  if(obj.contains("contains_filter") && obj["contains_filter"].is_string())
    st.contains_filter = obj["contains_filter"].get<std::string>();
  else
    st.contains_filter = first_non_empty_filter(obj.value("contains_filters", Json::array()));
  st.contains_filter_column = obj.value("contains_filter_column", -1);

  if(obj.contains("not_contains_filter") && obj["not_contains_filter"].is_string())
    st.not_contains_filter = obj["not_contains_filter"].get<std::string>();
  else
    st.not_contains_filter = first_non_empty_filter(obj.value("not_contains_filters", Json::array()));
  st.not_contains_filter_column = obj.value("not_contains_filter_column", -1);

  return st;
}

bool try_get_header_open_state(const std::string &doc_key, int header_id, bool &open_out)
{
  ensure_preview_state_loaded();

  const Json &docs = g_preview_state_json["documents"];
  auto doc_it = docs.find(doc_key);
  if(doc_it == docs.end() || !doc_it->is_object()) return false;
  const Json &headers = doc_it->value("headers", Json::object());
  auto header_it = headers.find(std::to_string(header_id));
  if(header_it == headers.end() || !header_it->is_boolean()) return false;
  open_out = header_it->get<bool>();
  return true;
}

void sync_header_open_state_to_json(const std::string &doc_key, int header_id, bool open)
{
  ensure_preview_state_loaded();

  Json &doc = g_preview_state_json["documents"][doc_key];
  if(!doc.is_object()) doc = Json::object();

  Json &headers = doc["headers"];
  if(!headers.is_object()) headers = Json::object();

  headers[std::to_string(header_id)] = open;
  g_preview_state_dirty = true;
}

void sync_table_state_to_json(const std::string &doc_key, int table_id, const TableViewState &st)
{
  ensure_preview_state_loaded();

  Json &doc = g_preview_state_json["documents"][doc_key];
  if(!doc.is_object()) doc = Json::object();

  Json &tables = doc["tables"];
  if(!tables.is_object()) tables = Json::object();

  Json &table = tables[std::to_string(table_id)];
  if(!table.is_object()) table = Json::object();

  table["sort_column"] = st.sort_column;
  table["sort_ascending"] = st.sort_ascending;
  table["contains_filter"] = st.contains_filter;
  table["contains_filter_column"] = st.contains_filter_column;
  table["not_contains_filter"] = st.not_contains_filter;
  table["not_contains_filter_column"] = st.not_contains_filter_column;
  table["contains_filters"] = Json::array({st.contains_filter});
  table["not_contains_filters"] = Json::array({st.not_contains_filter});

  g_preview_state_dirty = true;
}

void reset_table_view_state(TableViewState &state)
{
  state.sort_column = -1;
  state.sort_ascending = true;
  state.contains_filter.clear();
  state.contains_filter_column = -1;
  state.not_contains_filter.clear();
  state.not_contains_filter_column = -1;
}

TableViewState &table_state_for(const std::string &doc_key, int table_id)
{
  ensure_preview_state_loaded();

  auto &doc_map = g_table_state_cache[doc_key];
  auto it = doc_map.find(table_id);
  if(it != doc_map.end()) return it->second;

  TableViewState st;
  const Json &docs = g_preview_state_json["documents"];
  auto doc_it = docs.find(doc_key);
  if(doc_it != docs.end() && doc_it->is_object())
  {
    const Json &tables = doc_it->value("tables", Json::object());
    auto table_it = tables.find(std::to_string(table_id));
    if(table_it != tables.end()) st = parse_table_state_from_json(*table_it);
  }

  auto [inserted, _] = doc_map.emplace(table_id, std::move(st));
  return inserted->second;
}

void save_preview_state_if_dirty()
{
  if(!g_preview_state_dirty) return;
  ensure_preview_state_loaded();

  std::ofstream out(kMarkdownPreviewStateFile, std::ios::binary | std::ios::trunc);
  if(!out) return;
  out << g_preview_state_json.dump(2);
  g_preview_state_dirty = false;
}

bool set_all_preview_headers_open_impl(std::string_view document_path, std::string_view markdown, bool open)
{
  const std::string doc_key(document_path);
  if(doc_key.empty()) return false;

  bool changed = false;
  size_t pos = 0;
  while(pos < markdown.size())
  {
    const size_t line_start = pos;
    size_t line_end = markdown.find('\n', pos);
    const bool has_newline = (line_end != std::string::npos);
    if(!has_newline) line_end = markdown.size();

    const std::string_view line(markdown.data() + line_start, line_end - line_start);
    int heading_level = 0;
    std::string_view heading_title;
    if(parse_heading_line(line, heading_level, heading_title))
    {
      bool current_open = false;
      const bool has_saved_open = try_get_header_open_state(doc_key, static_cast<int>(line_start), current_open);
      if(!has_saved_open || current_open != open)
      {
        sync_header_open_state_to_json(doc_key, static_cast<int>(line_start), open);
        changed = true;
      }
    }

    pos = has_newline ? line_end + 1 : line_end;
  }

  if(changed) save_preview_state_if_dirty();
  return changed;
}

PreviewHeaderStateSummary summarize_preview_header_states_impl(std::string_view document_path, std::string_view markdown)
{
  PreviewHeaderStateSummary summary;
  const std::string doc_key(document_path);
  if(doc_key.empty()) return summary;

  size_t pos = 0;
  while(pos < markdown.size())
  {
    const size_t line_start = pos;
    size_t line_end = markdown.find('\n', pos);
    const bool has_newline = (line_end != std::string::npos);
    if(!has_newline) line_end = markdown.size();

    const std::string_view line(markdown.data() + line_start, line_end - line_start);
    int heading_level = 0;
    std::string_view heading_title;
    if(parse_heading_line(line, heading_level, heading_title))
    {
      summary.has_headers = true;
      bool current_open = false;
      const bool has_saved_open = try_get_header_open_state(doc_key, static_cast<int>(line_start), current_open);
      if(has_saved_open && current_open)
        summary.any_expanded = true;
      else
        summary.any_collapsed = true;
    }

    pos = has_newline ? line_end + 1 : line_end;
  }

  return summary;
}

std::string capture_preview_state_snapshot_impl()
{
  ensure_preview_state_loaded();

  Json root = Json::object();
  root["preview"] = g_preview_state_json;

  Json ui_state = Json::parse(MarkdownUi::capture_ui_state_snapshot(), nullptr, false);
  if(ui_state.is_discarded() || !ui_state.is_object()) ui_state = Json::object();
  root["ui"] = std::move(ui_state);
  return root.dump();
}

void apply_preview_state_snapshot_impl(std::string_view snapshot)
{
  g_preview_state_loaded = true;
  g_preview_state_dirty = false;
  g_table_state_cache.clear();
  g_cell_editor_state = CellEditorState{};
  g_filter_dialog_state = FilterDialogState{};

    Json root = Json::parse(snapshot.begin(), snapshot.end(), nullptr, false);
  Json preview_state;
  Json ui_state;
  if(root.is_discarded() || !root.is_object())
  {
    preview_state = Json::object();
    ui_state = Json::object();
  }
  else if(root.contains("preview") || root.contains("ui"))
  {
    preview_state = root.value("preview", Json::object());
    ui_state = root.value("ui", Json::object());
  }
  else
  {
    preview_state = std::move(root);
    ui_state = Json::object();
  }

  if(!preview_state.is_object()) preview_state = Json::object();
  if(!preview_state.contains("documents") || !preview_state["documents"].is_object())
    preview_state["documents"] = Json::object();
  g_preview_state_json = std::move(preview_state);
  MarkdownUi::apply_ui_state_snapshot(ui_state.dump());

  std::ofstream out(kMarkdownPreviewStateFile, std::ios::binary | std::ios::trunc);
  if(out) out << g_preview_state_json.dump(2);
}

std::vector<std::string> split_md_table_cells(std::string_view line)
{
  std::vector<std::string> cells;
  std::string_view t = NoteCore::trim(line);
  if(t.empty() || t.find('|') == std::string_view::npos) return cells;

  if(!t.empty() && t.front() == '|') t.remove_prefix(1);
  if(!t.empty() && t.back() == '|') t.remove_suffix(1);

  size_t start = 0;
  while(start <= t.size())
  {
    size_t sep = t.find('|', start);
    const size_t end = (sep == std::string_view::npos) ? t.size() : sep;
    cells.emplace_back(NoteCore::trim(t.substr(start, end - start)));
    if(sep == std::string_view::npos) break;
    start = sep + 1;
  }
  return cells;
}

bool is_md_table_separator(std::string_view line, size_t expected_cols)
{
  const std::vector<std::string> parts = split_md_table_cells(line);
  if(parts.size() != expected_cols || parts.empty()) return false;

  for(const std::string &p : parts)
  {
    std::string_view s = NoteCore::trim(p);
    if(s.empty()) return false;
    if(s.front() == ':') s.remove_prefix(1);
    if(!s.empty() && s.back() == ':') s.remove_suffix(1);
    if(s.size() < 3) return false;
    for(char c : s)
    {
      if(c != '-') return false;
    }
  }
  return true;
}

bool try_parse_markdown_table(
    const std::string &markdown,
    size_t line_start,
    size_t line_end,
    bool has_newline,
    ParsedMarkdownTable &out)
{
  out = ParsedMarkdownTable{};

  const std::string_view header_line(markdown.data() + line_start, line_end - line_start);
  const std::string_view header_trim = NoteCore::trim(header_line);
  if(header_trim.empty() || NoteCore::starts_with(header_trim, ">")) return false;

  std::vector<std::string> header = split_md_table_cells(header_line);
  if(header.size() < 2) return false;

  const size_t sep_start = has_newline ? (line_end + 1) : markdown.size();
  if(sep_start >= markdown.size()) return false;

  size_t sep_end = markdown.find('\n', sep_start);
  const bool sep_has_newline = (sep_end != std::string::npos);
  if(!sep_has_newline) sep_end = markdown.size();

  const std::string_view sep_line(markdown.data() + sep_start, sep_end - sep_start);
  if(!is_md_table_separator(sep_line, header.size())) return false;

  size_t scan = sep_has_newline ? (sep_end + 1) : markdown.size();
  std::vector<std::vector<std::string>> rows;
  while(scan < markdown.size())
  {
    size_t row_end = markdown.find('\n', scan);
    const bool row_has_newline = (row_end != std::string::npos);
    if(!row_has_newline) row_end = markdown.size();

    const std::string_view row_line(markdown.data() + scan, row_end - scan);
    const std::string_view row_trim = NoteCore::trim(row_line);
    if(row_trim.empty() || NoteCore::starts_with(row_trim, ">")) break;

    std::vector<std::string> row_cells = split_md_table_cells(row_line);
    if(row_cells.size() != header.size()) break;

    rows.push_back(std::move(row_cells));
    if(!row_has_newline)
    {
      scan = markdown.size();
      break;
    }
    scan = row_end + 1;
  }

  out.header = std::move(header);
  out.rows = std::move(rows);
  out.block_start = line_start;
  out.block_end = scan;
  out.trailing_newline = (scan > line_start && scan <= markdown.size() && markdown[scan - 1] == '\n');
  return true;
}

std::string normalize_table_cell_value(std::string_view in)
{
  std::string out;
  out.reserve(in.size() + 4);
  for(char c : in)
  {
    if(c == '\r' || c == '\n')
      out.push_back(' ');
    else if(c == '|')
    {
      out.push_back('\\');
      out.push_back('|');
    }
    else
      out.push_back(c);
  }
  const std::string_view t = NoteCore::trim(out);
  return std::string(t);
}

std::string build_md_table_line(const std::vector<std::string> &cells)
{
  std::string line;
  line.reserve(cells.size() * 8 + 4);
  line.push_back('|');
  for(const std::string &cell : cells)
  {
    line.push_back(' ');
    line += normalize_table_cell_value(cell);
    line += " |";
  }
  return line;
}

std::string build_md_table_separator(size_t cols)
{
  std::string line;
  line.reserve(cols * 6 + 2);
  line.push_back('|');
  for(size_t i = 0; i < cols; ++i) line += " --- |";
  return line;
}

std::string build_md_table_markdown(
    const std::vector<std::string> &header,
    const std::vector<std::vector<std::string>> &rows,
    bool trailing_newline)
{
  if(header.empty()) return {};

  std::string out;
  out += build_md_table_line(header);
  out.push_back('\n');
  out += build_md_table_separator(header.size());
  for(const auto &row : rows)
  {
    out.push_back('\n');
    out += build_md_table_line(row);
  }

  if(trailing_newline && (out.empty() || out.back() != '\n')) out.push_back('\n');
  return out;
}

std::string to_lower_ascii_copy(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for(char c : s)
  {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool contains_case_insensitive(std::string_view haystack, const std::string &needle_lower)
{
  if(needle_lower.empty()) return true;
  const std::string hay = to_lower_ascii_copy(haystack);
  return hay.find(needle_lower) != std::string::npos;
}

bool row_matches_filters(
    const std::vector<std::string> &row,
    const std::string &contains_lower,
    int contains_column,
    const std::string &not_contains_lower,
    int not_contains_column)
{
  bool matches_contains = contains_lower.empty();
  bool matches_not_contains = true;

  auto column_matches = [&](int column, const std::string &needle_lower, bool want_match) {
    if(needle_lower.empty()) return want_match;
    if(column >= 0 && static_cast<size_t>(column) < row.size())
    {
      return contains_case_insensitive(row[static_cast<size_t>(column)], needle_lower) == want_match;
    }

    for(const std::string &cell : row)
    {
      if(contains_case_insensitive(cell, needle_lower) == want_match) return true;
    }
    return false;
  };

  if(!contains_lower.empty()) matches_contains = column_matches(contains_column, contains_lower, true);
  if(!not_contains_lower.empty()) matches_not_contains = !column_matches(not_contains_column, not_contains_lower, true);

  return matches_contains && matches_not_contains;
}

std::vector<int> build_row_display_order(
    const std::vector<std::vector<std::string>> &rows,
    const TableViewState &state,
    size_t col_count)
{
  std::vector<int> order(rows.size());
  std::iota(order.begin(), order.end(), 0);

  const std::string contains_lower = to_lower_ascii_copy(NoteCore::trim(state.contains_filter));
  const std::string not_contains_lower = to_lower_ascii_copy(NoteCore::trim(state.not_contains_filter));

  if(!contains_lower.empty() || !not_contains_lower.empty())
  {
    order.erase(
        std::remove_if(
            order.begin(),
            order.end(),
            [&](int idx) {
              return !row_matches_filters(
                  rows[static_cast<size_t>(idx)],
                  contains_lower,
                  state.contains_filter_column,
                  not_contains_lower,
                  state.not_contains_filter_column);
            }),
        order.end());
  }

  if(state.sort_column >= 0 && static_cast<size_t>(state.sort_column) < col_count)
  {
    const int col = state.sort_column;
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](int a, int b) {
          const std::string ka = to_lower_ascii_copy(rows[static_cast<size_t>(a)][static_cast<size_t>(col)]);
          const std::string kb = to_lower_ascii_copy(rows[static_cast<size_t>(b)][static_cast<size_t>(col)]);
          return state.sort_ascending ? (ka < kb) : (ka > kb);
        });
  }

  return order;
}

std::string escape_csv_cell(std::string_view cell)
{
  bool needs_quotes = false;
  for(char c : cell)
  {
    if(c == ',' || c == '"' || c == '\n' || c == '\r')
    {
      needs_quotes = true;
      break;
    }
  }

  if(!needs_quotes) return std::string(cell);

  std::string out;
  out.reserve(cell.size() + 4);
  out.push_back('"');
  for(char c : cell)
  {
    if(c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string build_table_csv(
    const std::vector<std::string> &header,
    const std::vector<std::vector<std::string>> &rows,
    const std::vector<int> &display_order)
{
  std::string csv;
  auto append_row = [&](const std::vector<std::string> &r) {
    for(size_t c = 0; c < r.size(); ++c)
    {
      if(c > 0) csv.push_back(',');
      csv += escape_csv_cell(r[c]);
    }
    csv.push_back('\n');
  };

  append_row(header);
  for(int raw_idx : display_order)
  {
    if(raw_idx < 0 || static_cast<size_t>(raw_idx) >= rows.size()) continue;
    append_row(rows[static_cast<size_t>(raw_idx)]);
  }
  return csv;
}

void open_filter_dialog(
    const std::string &document_key,
    int table_id,
    int column,
    bool not_contains,
    const std::string &initial_text)
{
  g_filter_dialog_state.active = true;
  g_filter_dialog_state.open_request = true;
  g_filter_dialog_state.request_focus = true;
  g_filter_dialog_state.not_contains = not_contains;
  g_filter_dialog_state.document_key = document_key;
  g_filter_dialog_state.table_id = table_id;
  g_filter_dialog_state.column = column;

  const size_t doc_hash = std::hash<std::string>{}(document_key);
  g_filter_dialog_state.popup_id =
      std::string("Table Filter##") + std::to_string(table_id) + "_" + std::to_string(doc_hash);
  std::snprintf(
      g_filter_dialog_state.buffer,
      sizeof(g_filter_dialog_state.buffer),
      "%s",
      initial_text.c_str());
}

void render_filter_dialog(
    const std::string &document_key,
    int table_id,
    TableViewState &state,
    TableRenderOutcome &outcome)
{
  if(!g_filter_dialog_state.active) return;
  if(g_filter_dialog_state.document_key != document_key) return;
  if(g_filter_dialog_state.table_id != table_id) return;

  if(g_filter_dialog_state.open_request)
  {
    ImGui::OpenPopup(g_filter_dialog_state.popup_id.c_str());
    g_filter_dialog_state.open_request = false;
  }

  if(!g_filter_dialog_state.open_request &&
     !g_filter_dialog_state.popup_id.empty() &&
     !ImGui::IsPopupOpen(g_filter_dialog_state.popup_id.c_str()))
  {
    g_filter_dialog_state.active = false;
    g_filter_dialog_state.popup_id.clear();
    return;
  }

  if(!ImGui::BeginPopupModal(g_filter_dialog_state.popup_id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  if(g_filter_dialog_state.request_focus)
  {
    ImGui::SetKeyboardFocusHere();
    g_filter_dialog_state.request_focus = false;
  }

  ImGui::TextUnformatted(g_filter_dialog_state.not_contains ? "Filter by Not contains" : "Filter by Contains");
  const bool submit = ImGui::InputText(
      "##table_filter_input",
      g_filter_dialog_state.buffer,
      sizeof(g_filter_dialog_state.buffer),
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

  bool apply = submit;
  if(!apply) apply = ImGui::Button("Apply");
  ImGui::SameLine();
  const bool cancel = ImGui::Button("Cancel");

  if(apply)
  {
    const std::string value = std::string(NoteCore::trim(g_filter_dialog_state.buffer));
    bool changed = false;
    if(g_filter_dialog_state.not_contains)
    {
      changed = (state.not_contains_filter != value) || (state.not_contains_filter_column != g_filter_dialog_state.column);
      if(changed)
      {
        state.not_contains_filter = value;
        state.not_contains_filter_column = value.empty() ? -1 : g_filter_dialog_state.column;
      }
    }
    else
    {
      changed = (state.contains_filter != value) || (state.contains_filter_column != g_filter_dialog_state.column);
      if(changed)
      {
        state.contains_filter = value;
        state.contains_filter_column = value.empty() ? -1 : g_filter_dialog_state.column;
      }
    }

    if(changed)
    {
      sync_table_state_to_json(document_key, table_id, state);
      outcome.preview_state_changed = true;
    }

    g_filter_dialog_state.active = false;
    g_filter_dialog_state.popup_id.clear();
    ImGui::CloseCurrentPopup();
  }

  if(cancel)
  {
    g_filter_dialog_state.active = false;
    g_filter_dialog_state.popup_id.clear();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

TableRenderOutcome render_interactive_table(
    const ParsedMarkdownTable &parsed,
    int table_id,
    const std::string &document_key)
{
  TableRenderOutcome outcome;
  std::vector<std::string> header = parsed.header;
  std::vector<std::vector<std::string>> rows = parsed.rows;

  if(header.empty()) return outcome;

  const int cols = static_cast<int>(header.size());
  TableViewState &state = table_state_for(document_key, table_id);
  if(state.sort_column >= cols)
  {
    state.sort_column = -1;
    sync_table_state_to_json(document_key, table_id, state);
    outcome.preview_state_changed = true;
  }

  if(g_cell_editor_state.active &&
     g_cell_editor_state.document_key == document_key &&
     g_cell_editor_state.table_id == table_id)
  {
    const bool invalid_col = (g_cell_editor_state.column < 0 || g_cell_editor_state.column >= cols);
    const bool invalid_row =
        (!g_cell_editor_state.header &&
         (g_cell_editor_state.raw_row < 0 || static_cast<size_t>(g_cell_editor_state.raw_row) >= rows.size()));
    if(invalid_col || invalid_row)
    {
      g_cell_editor_state.active = false;
      g_cell_editor_state.was_active_last_frame = false;
    }
  }

  std::vector<int> display_order = build_row_display_order(rows, state, header.size());
  bool table_model_changed = false;

  enum class TableActionType
  {
    None,
    AddRow,
    AddColumn,
    RemoveRow,
    RemoveColumn
  };

  TableActionType pending_action = TableActionType::None;
  bool pending_header_cell = false;
  int pending_raw_row = -1;
  int pending_column = -1;
  bool pending_edit_navigation = false;
  bool pending_nav_header = false;
  int pending_nav_raw_row = -1;
  int pending_nav_column = -1;

  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders |
      ImGuiTableFlags_RowBg |
      ImGuiTableFlags_SizingStretchSame |
      ImGuiTableFlags_NoHostExtendX;

  auto column_has_contains_filter = [&](int column) {
    return !NoteCore::trim(state.contains_filter).empty() && state.contains_filter_column == column;
  };
  auto column_has_not_contains_filter = [&](int column) {
    return !NoteCore::trim(state.not_contains_filter).empty() && state.not_contains_filter_column == column;
  };
  auto decorated_header_label = [&](const std::string &label, int column) {
    std::string out = label;
    if(state.sort_column == column) out += state.sort_ascending ? " ^" : " v";
    if(column_has_contains_filter(column)) out += " [F]";
    if(column_has_not_contains_filter(column)) out += " [!F]";
    return out;
  };
  auto queue_horizontal_navigation = [&](bool header_cell, int raw_row, int column, int step) {
    if(cols <= 0 || step == 0) return;
    const int target_column = column + step;
    if(target_column < 0 || target_column >= cols) return;

    pending_nav_header = header_cell;
    pending_nav_raw_row = header_cell ? -1 : raw_row;
    pending_nav_column = target_column;
    pending_edit_navigation = true;
  };
  auto queue_vertical_navigation = [&](bool header_cell, int raw_row, int column, int step) {
    if(cols <= 0 || step == 0) return;
    if(column < 0 || column >= cols) return;

    if(header_cell)
    {
      if(step > 0)
      {
        if(rows.empty()) return;
        pending_nav_header = false;
        pending_nav_raw_row = 0;
        pending_nav_column = column;
        pending_edit_navigation = true;
      }
      return;
    }

    const int target_row = raw_row + step;
    if(target_row < 0)
    {
      pending_nav_header = true;
      pending_nav_raw_row = -1;
      pending_nav_column = column;
      pending_edit_navigation = true;
      return;
    }
    if(static_cast<size_t>(target_row) >= rows.size()) return;

    pending_nav_header = false;
    pending_nav_raw_row = target_row;
    pending_nav_column = column;
    pending_edit_navigation = true;
  };

  ImGui::PushID(table_id);
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.22f, 0.23f, 0.26f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.04f));

  if(ImGui::BeginTable("##md_table_interactive", cols, flags))
  {
    for(int c = 0; c < cols; ++c)
    {
      const std::string col_id = "##col_" + std::to_string(c);
      ImGui::TableSetupColumn(col_id.c_str());
    }

    auto render_cell = [&](bool header_cell, int raw_row, int column, std::string &cell) {
      ImGui::TableSetColumnIndex(column);
      ImGui::PushID(header_cell ? (100000 + column) : (raw_row * 1000 + column));

      const bool editing_this_cell =
          g_cell_editor_state.active &&
          g_cell_editor_state.document_key == document_key &&
          g_cell_editor_state.table_id == table_id &&
          g_cell_editor_state.header == header_cell &&
          g_cell_editor_state.column == column &&
          g_cell_editor_state.raw_row == raw_row;

      if(editing_this_cell)
      {
        if(g_cell_editor_state.request_focus)
        {
          ImGui::SetKeyboardFocusHere();
          g_cell_editor_state.request_focus = false;
        }

        const bool submit = ImGui::InputText(
            "##cell_edit",
            g_cell_editor_state.buffer,
            sizeof(g_cell_editor_state.buffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        const bool item_active = ImGui::IsItemActive();
        const bool tab_pressed = item_active && ImGui::IsKeyPressed(ImGuiKey_Tab, false);
        const bool reverse_tab = tab_pressed && ImGui::GetIO().KeyShift;
        const bool enter_pressed =
            item_active &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        const bool reverse_enter = (enter_pressed || submit) && ImGui::GetIO().KeyShift;
        if(submit || tab_pressed || enter_pressed)
        {
          const std::string value = normalize_table_cell_value(g_cell_editor_state.buffer);
          if(value != cell)
          {
            cell = value;
            table_model_changed = true;
          }
          g_cell_editor_state.active = false;
          g_cell_editor_state.was_active_last_frame = false;
          if(tab_pressed)
            queue_horizontal_navigation(header_cell, raw_row, column, reverse_tab ? -1 : 1);
          else if(enter_pressed || submit)
            queue_vertical_navigation(header_cell, raw_row, column, reverse_enter ? -1 : 1);
        }
        else if(item_active && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
          g_cell_editor_state.active = false;
          g_cell_editor_state.was_active_last_frame = false;
        }
        else if(!item_active && g_cell_editor_state.was_active_last_frame)
        {
          g_cell_editor_state.active = false;
          g_cell_editor_state.was_active_last_frame = false;
        }
        else
        {
          g_cell_editor_state.was_active_last_frame = item_active;
        }
      }
      else
      {
        const float hitbox_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
        const ImVec2 cell_start = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        const std::string display = header_cell ? decorated_header_label(cell, column) : cell;
        if(display.empty())
        {
          ImGui::TextUnformatted(" ");
        }
        else
        {
          MarkdownView::render_inline(display);
        }
        ImGui::EndGroup();
        const ImVec2 cell_end = ImGui::GetCursorScreenPos();
        const float hitbox_height = std::max(cell_end.y - cell_start.y, ImGui::GetTextLineHeightWithSpacing());
        ImGui::SetCursorScreenPos(cell_start);
        ImGui::InvisibleButton("##cell_hitbox", ImVec2(hitbox_width, hitbox_height));
        const bool cell_hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(cell_end);

        if(cell_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
          open_table_cell_editor(document_key, table_id, header_cell, raw_row, column, cell);
          outcome.consumed_double_click = true;
        }
      }

      if(ImGui::BeginPopupContextItem("##table_cell_ctx", ImGuiPopupFlags_MouseButtonRight))
      {
        outcome.consumed_right_click = true;

        if(ImGui::MenuItem("Add row"))
        {
          pending_action = TableActionType::AddRow;
          pending_header_cell = header_cell;
          pending_raw_row = raw_row;
          pending_column = column;
        }

        if(ImGui::MenuItem("Add column"))
        {
          pending_action = TableActionType::AddColumn;
          pending_header_cell = header_cell;
          pending_raw_row = raw_row;
          pending_column = column;
        }

        const bool can_remove_row = (!header_cell && !rows.empty() && raw_row >= 0 && static_cast<size_t>(raw_row) < rows.size());
        if(ImGui::MenuItem("Remove row", nullptr, false, can_remove_row))
        {
          pending_action = TableActionType::RemoveRow;
          pending_header_cell = header_cell;
          pending_raw_row = raw_row;
          pending_column = column;
        }

        const bool can_remove_col = header.size() > 2;
        if(ImGui::MenuItem("Remove column", nullptr, false, can_remove_col))
        {
          pending_action = TableActionType::RemoveColumn;
          pending_header_cell = header_cell;
          pending_raw_row = raw_row;
          pending_column = column;
        }

        if(ImGui::MenuItem("Copy to csv"))
        {
          const std::vector<int> order_for_copy = build_row_display_order(rows, state, header.size());
          const std::string csv = build_table_csv(header, rows, order_for_copy);
          ImGui::SetClipboardText(csv.c_str());
        }

        if(ImGui::MenuItem("Sort A-Z"))
        {
          if(state.sort_column != column || !state.sort_ascending)
          {
            state.sort_column = column;
            state.sort_ascending = true;
            sync_table_state_to_json(document_key, table_id, state);
            outcome.preview_state_changed = true;
          }
        }

        if(ImGui::MenuItem("Sort Z-A"))
        {
          if(state.sort_column != column || state.sort_ascending)
          {
            state.sort_column = column;
            state.sort_ascending = false;
            sync_table_state_to_json(document_key, table_id, state);
            outcome.preview_state_changed = true;
          }
        }

        if(ImGui::MenuItem("Filter by Contains"))
        {
          open_filter_dialog(document_key, table_id, column, false, state.contains_filter);
        }

        if(ImGui::MenuItem("Filter by Not contains"))
        {
          open_filter_dialog(document_key, table_id, column, true, state.not_contains_filter);
        }

        const bool has_any_filter_or_sort =
            state.sort_column >= 0 ||
            !NoteCore::trim(state.contains_filter).empty() ||
            !NoteCore::trim(state.not_contains_filter).empty();
        if(ImGui::MenuItem("Clear filters", nullptr, false, has_any_filter_or_sort))
        {
          reset_table_view_state(state);
          sync_table_state_to_json(document_key, table_id, state);
          outcome.preview_state_changed = true;
        }

        ImGui::EndPopup();
      }

      ImGui::PopID();
    };

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for(int c = 0; c < cols; ++c) render_cell(true, -1, c, header[static_cast<size_t>(c)]);

    for(int raw_idx : display_order)
    {
      if(raw_idx < 0 || static_cast<size_t>(raw_idx) >= rows.size()) continue;
      ImGui::TableNextRow();
      for(int c = 0; c < cols; ++c)
      {
        render_cell(false, raw_idx, c, rows[static_cast<size_t>(raw_idx)][static_cast<size_t>(c)]);
      }
    }

    ImGui::EndTable();
  }

  if(pending_action != TableActionType::None)
  {
    if(g_cell_editor_state.active &&
       g_cell_editor_state.document_key == document_key &&
       g_cell_editor_state.table_id == table_id)
    {
      g_cell_editor_state.active = false;
      g_cell_editor_state.was_active_last_frame = false;
    }

    switch(pending_action)
    {
    case TableActionType::AddRow: {
      const size_t insert_at = pending_header_cell
                                   ? 0u
                                   : static_cast<size_t>(std::max(0, pending_raw_row + 1));
      std::vector<std::string> new_row(header.size(), std::string{});
      rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(std::min(insert_at, rows.size())), std::move(new_row));
      table_model_changed = true;
      break;
    }
    case TableActionType::AddColumn: {
      const int current_cols = static_cast<int>(header.size());
      const size_t insert_at = static_cast<size_t>(std::clamp(pending_column + 1, 0, current_cols));
      header.insert(
          header.begin() + static_cast<std::ptrdiff_t>(insert_at),
          "Column " + std::to_string(current_cols + 1));
      for(auto &row : rows)
      {
        row.insert(row.begin() + static_cast<std::ptrdiff_t>(std::min(insert_at, row.size())), "");
      }

      if(state.sort_column >= static_cast<int>(insert_at))
      {
        state.sort_column += 1;
        sync_table_state_to_json(document_key, table_id, state);
        outcome.preview_state_changed = true;
      }

      table_model_changed = true;
      break;
    }
    case TableActionType::RemoveRow: {
      if(pending_raw_row >= 0 && static_cast<size_t>(pending_raw_row) < rows.size())
      {
        rows.erase(rows.begin() + pending_raw_row);
        table_model_changed = true;
      }
      break;
    }
    case TableActionType::RemoveColumn: {
      if(header.size() > 2 && pending_column >= 0 && static_cast<size_t>(pending_column) < header.size())
      {
        header.erase(header.begin() + pending_column);
        for(auto &row : rows)
        {
          if(pending_column >= 0 && static_cast<size_t>(pending_column) < row.size())
            row.erase(row.begin() + pending_column);
        }

        bool state_changed = false;
        if(state.sort_column == pending_column)
        {
          state.sort_column = -1;
          state_changed = true;
        }
        else if(state.sort_column > pending_column)
        {
          state.sort_column -= 1;
          state_changed = true;
        }
        if(state_changed)
        {
          sync_table_state_to_json(document_key, table_id, state);
          outcome.preview_state_changed = true;
        }

        table_model_changed = true;
      }
      break;
    }
    case TableActionType::None:
      break;
    }
  }

  render_filter_dialog(document_key, table_id, state, outcome);

  if(pending_edit_navigation &&
     pending_nav_column >= 0 &&
     pending_nav_column < cols)
  {
    const std::string *target_cell = nullptr;
    if(pending_nav_header)
    {
      target_cell = &header[static_cast<size_t>(pending_nav_column)];
    }
    else if(pending_nav_raw_row >= 0 && static_cast<size_t>(pending_nav_raw_row) < rows.size())
    {
      target_cell = &rows[static_cast<size_t>(pending_nav_raw_row)][static_cast<size_t>(pending_nav_column)];
    }

    if(target_cell)
      open_table_cell_editor(document_key, table_id, pending_nav_header, pending_nav_raw_row, pending_nav_column, *target_cell);
  }

  ImGui::PopStyleColor(2);
  ImGui::PopID();

  if(table_model_changed)
  {
    outcome.has_replacement = true;
    outcome.markdown_changed = true;
    outcome.replacement = build_md_table_markdown(header, rows, parsed.trailing_newline);
  }

  return outcome;
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

void insert_markdown_table_at_cursor(std::string &text, MdFormatState &fmt, int rows, int cols)
{
  int p = std::max(0, std::min(fmt.cursor_pos, static_cast<int>(text.size())));
  std::string ins;
  if(p > 0 && text[static_cast<size_t>(p) - 1] != '\n') ins.push_back('\n');
  const int safe_rows = std::max(1, rows);
  const int safe_cols = std::max(1, cols);
  const int header_offset = static_cast<int>(ins.size()) + 2;

  ins += "|";
  for(int col = 0; col < safe_cols; ++col)
  {
    ins += " Header " + std::to_string(col + 1) + " |";
  }
  ins += "\n|";
  for(int col = 0; col < safe_cols; ++col)
  {
    ins += " --- |";
  }
  ins += "\n";
  for(int row = 0; row < safe_rows; ++row)
  {
    ins += "|";
    for(int col = 0; col < safe_cols; ++col)
    {
      ins += " Cell " + std::to_string(row + 1) + "," + std::to_string(col + 1) + " |";
    }
    ins += "\n";
  }
  text.insert(static_cast<size_t>(p), ins);

  const int cursor = p + header_offset;
  fmt.cursor_pos = cursor;
  fmt.sel_start = cursor;
  fmt.sel_end = cursor + 8;
  fmt.selection_anchor = cursor;
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

std::pair<int, int> word_bounds_from_double_click(const std::string &text, int cursor_pos, int sel_start, int sel_end)
{
  const int n = static_cast<int>(text.size());
  const int cursor = std::max(0, std::min(cursor_pos, n));
  const int sel_lo = std::max(0, std::min(std::min(sel_start, sel_end), n));
  const int sel_hi = std::max(0, std::min(std::max(sel_start, sel_end), n));

  auto try_pos = [&](int pos, std::pair<int, int> &bounds_out) -> bool {
    if(pos < sel_lo || pos >= sel_hi) return false;
    if(pos < 0 || pos >= n) return false;
    if(!is_word_char(text[static_cast<size_t>(pos)])) return false;
    bounds_out = expand_word_bounds(text, pos);
    return true;
  };

  std::pair<int, int> bounds(cursor, cursor);
  if(try_pos(cursor, bounds)) return bounds;
  if(try_pos(cursor - 1, bounds)) return bounds;

  for(int dist = 1; sel_lo + dist <= sel_hi || cursor - dist >= sel_lo; ++dist)
  {
    if(try_pos(cursor - 1 - dist, bounds)) return bounds;
    if(try_pos(cursor + dist, bounds)) return bounds;
  }

  return {cursor, cursor};
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

void set_preview_document_path(std::string_view path)
{
  g_preview_document_path.assign(path.data(), path.size());
  MarkdownView::set_document_path(path);
  MarkdownUi::set_widget_document_path(std::filesystem::path(path));
}

PreviewRenderResult render_preview_with_task_checkboxes_ex(std::string &markdown)
{
  PreviewRenderResult result;
  bool checkbox_changed = false;
  std::vector<TableReplacement> table_replacements;

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

    const float cur_y      = ImGui::GetCursorScreenPos().y;
    const float win_top    = ImGui::GetWindowPos().y;
    const float win_bottom = win_top + ImGui::GetWindowHeight();
    const float margin     = 64.f; // px buffer around the fold

    if(cur_y > win_bottom + margin || cur_y < win_top - margin)
    {
      // Chunk is off-screen: skip expensive text layout, advance cursor by estimated height.
      // Line count × line height is a conservative approximation; scroll position stays sane.
      int lines = 1;
      for(char c : normal_chunk) if(c == '\n') ++lines;
      ImGui::Dummy(ImVec2(1.f, static_cast<float>(lines) * ImGui::GetTextLineHeightWithSpacing()));
    }
    else
    {
      MarkdownView::render(normal_chunk);
    }
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
      const std::string doc_key = current_document_key();
      bool saved_open = false;
      const bool has_saved_open = try_get_header_open_state(doc_key, static_cast<int>(line_start), saved_open);
      if(g_force_open_preview_headers) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      else if(has_saved_open) ImGui::SetNextItemOpen(saved_open, ImGuiCond_Always);
      const bool open = ImGui::TreeNodeEx(
          reinterpret_cast<void *>(static_cast<intptr_t>(static_cast<int>(line_start) + 0x10000)),
          ImGuiTreeNodeFlags_SpanAvailWidth | (g_force_open_preview_headers ? ImGuiTreeNodeFlags_DefaultOpen : 0),
          "%s",
          std::string(heading_title).c_str());
      if(!g_force_open_preview_headers && (!has_saved_open || saved_open != open))
      {
        sync_header_open_state_to_json(doc_key, static_cast<int>(line_start), open);
        result.preview_state_changed = true;
      }
      header_stack.push_back(HeaderUi{heading_level, open});
      pos = has_newline ? line_end + 1 : line_end;
      continue;
    }

    if(!all_headers_open())
    {
      pos = has_newline ? line_end + 1 : line_end;
      continue;
    }

    if(tline == "```UI")
    {
      size_t scan = has_newline ? line_end + 1 : line_end;
      size_t block_end = markdown.size();
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
        scan = ln ? le + 1 : le;
      }

      if(closed)
      {
        flush_chunk();
        const MarkdownUi::RenderResult ui_result = MarkdownUi::try_render_ui_block(markdown, line_start, line_end, block_end);
        result.markdown_changed = result.markdown_changed || ui_result.markdown_changed;
        result.preview_state_changed = result.preview_state_changed || ui_result.preview_state_changed;
        result.consumed_right_click = result.consumed_right_click || ui_result.consumed_right_click;
        pos = block_end;
        continue;
      }
    }

    if(tline == "```ui-mermaid")
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
        flush_chunk();
        const std::string resolved = MarkdownUi::resolve_ui_mermaid_template(markdown, body);
        std::string mermaid_type;
        if(detect_mermaid_type(resolved, mermaid_type))
          render_mermaid_block(mermaid_type, resolved, static_cast<int>(line_start));
        else
          normal_chunk.append(markdown.data() + line_start, block_end - line_start);
        pos = block_end;
        continue;
      }
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

    ParsedMarkdownTable parsed_table;
    if(try_parse_markdown_table(markdown, line_start, line_end, has_newline, parsed_table))
    {
      flush_chunk();

      const int table_id = static_cast<int>(parsed_table.block_start);
      TableRenderOutcome table_out = render_interactive_table(parsed_table, table_id, current_document_key());

      result.preview_state_changed = result.preview_state_changed || table_out.preview_state_changed;
      result.consumed_right_click = result.consumed_right_click || table_out.consumed_right_click;
      result.consumed_double_click = result.consumed_double_click || table_out.consumed_double_click;

      if(table_out.has_replacement)
      {
        table_replacements.push_back(TableReplacement{
            parsed_table.block_start,
            parsed_table.block_end,
            std::move(table_out.replacement)});
      }

      pos = parsed_table.block_end;
      continue;
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
        checkbox_changed = true;
      }
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      if(checked)
      {
        std::string struck_label("~~");
        struck_label.append(label);
        struck_label.append("~~");
        MarkdownView::render_inline(struck_label);
      }
      else
      {
        MarkdownView::render_inline(std::string(label));
      }
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

  for(auto it = table_replacements.rbegin(); it != table_replacements.rend(); ++it)
  {
    if(it->end < it->start || it->end > markdown.size()) continue;
    markdown.replace(it->start, it->end - it->start, it->replacement);
  }

  result.markdown_changed = result.markdown_changed || checkbox_changed || !table_replacements.empty();
  if(result.preview_state_changed) save_preview_state_if_dirty();
  render_link_hover_preview_popup();

  const auto img_ctx = MarkdownView::render_image_context_menu(markdown);
  result.consumed_right_click = result.consumed_right_click || img_ctx.consumed_right_click;
  result.markdown_changed     = result.markdown_changed     || img_ctx.markdown_changed;

  return result;
}

std::string capture_preview_state_snapshot()
{
  return capture_preview_state_snapshot_impl();
}

void apply_preview_state_snapshot(std::string_view snapshot)
{
  apply_preview_state_snapshot_impl(snapshot);
}

bool render_preview_with_task_checkboxes(std::string &markdown)
{
  return render_preview_with_task_checkboxes_ex(markdown).markdown_changed;
}

PreviewHeaderStateSummary summarize_preview_header_states(std::string_view document_path, std::string_view markdown)
{
  return summarize_preview_header_states_impl(document_path, markdown);
}

bool set_all_preview_headers_open(std::string_view document_path, std::string_view markdown, bool open)
{
  return set_all_preview_headers_open_impl(document_path, markdown, open);
}
} // namespace MarkdownSupport
