#include "markdown_support.hpp"

#include "atomic_file.hpp"
#include "markdown_sections.hpp"
#include "markdown_view.hpp"
#include "markdown_widgets.hpp"
#include "project_paths.hpp"
#include "mermaid.hpp"
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
#include <optional>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace MarkdownSupport
{
PreviewRenderResult render_preview_with_task_checkboxes_ex(std::string &markdown);
void set_preview_document_path(std::string_view path);

namespace
{
static ImVec2 nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

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
  return StringUtils::trim(line.substr(i)).empty();
}

bool is_empty_quote_line(std::string_view line)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size() || line[i] != '>') return false;
  ++i;
  if(i < line.size() && line[i] == ' ') ++i;
  return StringUtils::trim(line.substr(i)).empty();
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
    std::string_view line = StringUtils::trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;

    if(!saw_pie)
    {
      if(!StringUtils::starts_with(line, "pie")) return false;
      saw_pie = true;
      const std::string_view rest = StringUtils::trim(line.substr(3));
      if(StringUtils::starts_with(rest, "title "))
        out.title = std::string(StringUtils::trim(rest.substr(6)));
      continue;
    }

    if(StringUtils::starts_with(line, "title "))
    {
      out.title = std::string(StringUtils::trim(line.substr(6)));
      continue;
    }

    const size_t col = line.find(':');
    if(col == std::string_view::npos) continue;

    std::string_view left = StringUtils::trim(line.substr(0, col));
    const std::string_view right = StringUtils::trim(line.substr(col + 1));
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
  const std::string t = StringUtils::to_lower_copy(token);
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
    const std::string_view line = StringUtils::trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;
    if(StringUtils::starts_with(line, "%%")) continue;
    if(StringUtils::starts_with(line, "%%{")) continue;
    if(line == "---")
    {
      while(p < body.size())
      {
        size_t e2 = body.find('\n', p);
        if(e2 == std::string_view::npos) e2 = body.size();
        const std::string_view fm_line = StringUtils::trim(body.substr(p, e2 - p));
        p = (e2 < body.size()) ? e2 + 1 : e2;
        if(fm_line == "---") break;
      }
      continue;
    }

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

  const float stable_w = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize;
  const float chart_w = std::floor(std::max(120.0f, std::min(240.0f, std::max(0.0f, stable_w) * 0.45f)));
  const float chart_h = chart_w;

  ImGui::PushID(id);
  ImGui::BeginGroup();
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##pie_canvas", nonzero_invisible_button_size(chart_w, chart_h));
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
  const std::string mt = StringUtils::to_lower_copy(mermaid_type);

  // ── already-rendered types ──────────────────────────────────────────────
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

// helper macro: parse + render or fall back to placeholder
#define MERMAID_DISPATCH(parse_fn, render_fn, DiagramType) \
  {                                                        \
    MermaidDiagrams::DiagramType d;                        \
    if(MermaidDiagrams::parse_fn(body, d))                 \
      MermaidDiagrams::render_fn(d, id);                   \
    else                                                   \
      render_mermaid_placeholder(mermaid_type, body, id);  \
    return;                                                \
  }

  if(mt == "sequencediagram") MERMAID_DISPATCH(parse_sequence, render_sequence, SequenceDiagram)
  if(mt == "classdiagram") MERMAID_DISPATCH(parse_class, render_class, ClassDiagram)
  if(mt == "statediagram" || mt == "statediagram-v2") MERMAID_DISPATCH(parse_state, render_state, StateDiagram)
  if(mt == "erdiagram") MERMAID_DISPATCH(parse_er, render_er, ERDiagram)
  if(mt == "journey") MERMAID_DISPATCH(parse_journey, render_journey, JourneyDiagram)
  if(mt == "gantt") MERMAID_DISPATCH(parse_gantt, render_gantt, GanttDiagram)
  if(mt == "quadrantchart") MERMAID_DISPATCH(parse_quadrant, render_quadrant, QuadrantDiagram)
  if(mt == "requirementdiagram") MERMAID_DISPATCH(parse_requirement, render_requirement, RequirementDiagram)
  if(mt == "gitgraph") MERMAID_DISPATCH(parse_git, render_git, GitDiagram)
  if(mt == "mindmap") MERMAID_DISPATCH(parse_mindmap, render_mindmap, MindmapDiagram)
  if(mt == "timeline") MERMAID_DISPATCH(parse_timeline, render_timeline, TimelineDiagram)
  if(mt == "sankey-beta" || mt == "sankey") MERMAID_DISPATCH(parse_sankey, render_sankey, SankeyDiagram)
  if(mt == "xychart-beta" || mt == "xychart") MERMAID_DISPATCH(parse_xychart, render_xychart, XYDiagram)
  if(mt == "block-beta" || mt == "block") MERMAID_DISPATCH(parse_block, render_block, BlockDiagram)
  if(mt == "packet-beta" || mt == "packet") MERMAID_DISPATCH(parse_packet, render_packet, PacketDiagram)
  if(mt == "kanban") MERMAID_DISPATCH(parse_kanban, render_kanban, KanbanDiagram)
  if(mt == "architecture-beta" || mt == "architecture") MERMAID_DISPATCH(parse_architecture, render_architecture, ArchDiagram)
  if(mt == "radar-beta" || mt == "radar") MERMAID_DISPATCH(parse_radar, render_radar, RadarDiagram)
  if(mt == "treemap-beta" || mt == "treemap") MERMAID_DISPATCH(parse_treemap, render_treemap, TreemapDiagram)
  if(mt == "eventmodeling") MERMAID_DISPATCH(parse_eventmodeling, render_eventmodeling, EventModelingDiagram)
  if(mt == "venn") MERMAID_DISPATCH(parse_venn, render_venn, VennDiagram)
  if(mt == "ishikawa") MERMAID_DISPATCH(parse_ishikawa, render_ishikawa, IshikawaDiagram)
  if(mt == "wardley") MERMAID_DISPATCH(parse_wardley, render_wardley, WardleyDiagram)
  if(mt == "treeview") MERMAID_DISPATCH(parse_treeview, render_treeview, TreeViewDiagram)
  if(mt == "zenuml")
  {
    MermaidDiagrams::SequenceDiagram d;
    if(MermaidDiagrams::parse_zenuml(body, d))
      MermaidDiagrams::render_zenuml(d, id);
    else
      render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }
#undef MERMAID_DISPATCH

  render_mermaid_placeholder(mermaid_type, body, id);
}

using Json = nlohmann::json;

static std::string g_preview_state_file = DATA_PATH "/markdown_preview_state.json";
static atomic_file::SnapshotStore &g_preview_files = atomic_file::shared_snapshot_store();
static atomic_file::PersistenceGuard g_preview_persistence_guard;
static std::unordered_map<std::string, std::string> g_preview_persistence_errors;

void record_preview_read(const std::filesystem::path &path,
                         const atomic_file::ReadResult &result)
{
  g_preview_persistence_guard.record_read(path, result);
  const std::string key = path.lexically_normal().generic_string();
  if(result && !g_preview_persistence_guard.has_preserved_stale(path))
    g_preview_persistence_errors.erase(key);
  else if(!result)
    g_preview_persistence_errors[key] = result.message;
}

bool save_preview_file(const std::filesystem::path &path, std::string_view content)
{
  const std::string key = path.lexically_normal().generic_string();
  if(!g_preview_persistence_guard.may_write(path))
  {
    g_preview_persistence_errors[key] =
        "canonical file was not read successfully: " +
        g_preview_persistence_guard.read_error(path);
    return false;
  }
  if(g_preview_persistence_guard.suppresses(path, content)) return false;

  const auto result = g_preview_files.save(path, content);
  g_preview_persistence_guard.record_save(path, content, result);
  if(result)
  {
    g_preview_persistence_errors.erase(key);
    return true;
  }
  std::string error = "cannot save '" + path.generic_string() + "': " + result.message;
  if(!result.recovery_path.empty())
    error += " Recovery: " + result.recovery_path.generic_string();
  g_preview_persistence_errors[key] = std::move(error);
  return false;
}

struct TableViewState
{
  int sort_column = -1;
  bool sort_ascending = true;
  std::string contains_filter;
  int contains_filter_column = -1;
  std::string not_contains_filter;
  int not_contains_filter_column = -1;
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
std::string g_preview_document_key;
std::filesystem::path g_preview_project_root;
std::optional<notepp::project_paths::ProjectPaths> g_preview_project_paths;
std::string g_preview_notes_top_level = "notes";
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

// Splice `new_section` into `file_content` at [start, end) and return the
// result. Caller is responsible for the range being valid for file_content.
// Returns the original `file_content` unchanged when the range is invalid.
static std::string splice_section(const std::string &file_content,
                                  size_t start,
                                  size_t end,
                                  const std::string &new_section)
{
  if(start > file_content.size() || end > file_content.size() || start > end)
    return file_content;
  std::string out;
  out.reserve(file_content.size() - (end - start) + new_section.size());
  out.append(file_content, 0, start);
  out.append(new_section);
  out.append(file_content, end, std::string::npos);
  return out;
}

// Persist an interactive edit made in the hover preview back to the source
// file at `preview.path`. Returns true when a write was actually performed.
//
// On success, also refreshes the cached hover-preview body so the next frame
// re-renders the edited content instead of the stale original. This is what
// makes the in-popup edits visible: without it, every frame re-seeds
// preview_markdown from the cached original and the user sees the change
// snap back to the pre-edit state.
//
// Defensive: refuses to write when the section range is unknown, the path
// is empty, or the file is too small to contain the section. We intentionally
// do NOT compare the on-disk content against the original snapshot: a single
// hover can produce multiple consecutive edits (e.g. toggling several
// checkboxes), and the strict compare would reject every edit after the
// first. This matches the main note view's write behaviour.
static bool persist_hover_preview_edit(const MarkdownHoverPreviewData &preview,
                                       const std::string &modified_body)
{
  if(preview.section_start == std::string_view::npos || preview.section_end == std::string_view::npos)
    return false;
  if(preview.path.empty()) return false;
  if(preview.section_start > preview.section_end) return false;

  const auto loaded = g_preview_files.load(preview.path);
  record_preview_read(preview.path, loaded);
  const std::string preview_key = std::filesystem::path(preview.path).lexically_normal().generic_string();
  if(!loaded || !loaded.snapshot.existed)
  {
    if(!loaded) g_preview_persistence_errors[preview_key] = loaded.message;
    return false;
  }
  g_preview_persistence_errors.erase(preview_key);
  const std::string &file_content = loaded.snapshot.content;
  if(preview.section_end > file_content.size()) return false;

  const std::string updated = splice_section(file_content,
                                             preview.section_start,
                                             preview.section_end,
                                             modified_body);
  if(updated == file_content) return false; // nothing actually changed

  if(!save_preview_file(preview.path, updated)) return false;

  // Refresh the cached body so the next frame renders the edited content.
  MarkdownView::update_hover_preview_body(modified_body);
  return true;
}

void render_link_hover_preview_popup()
{
  if(g_rendering_hover_preview) return;

  const int frame = ImGui::GetFrameCount();
  if(g_hover_preview_drawn_frame == frame) return;

  MarkdownHoverPreviewData preview;
  if(!MarkdownView::take_hover_preview(preview)) return;

  // Larger, user-resizable popup that can scroll. Sized to a typical preview
  // column on a 1440 px screen; the user can still resize the corner.
  constexpr ImVec2 kPreviewMinSize(360.0f, 180.0f);
  constexpr ImVec2 kPreviewMaxSize(960.0f, 720.0f);
  // Default size for first appearance of each link's preview window.
  // Applied with ImGuiCond_Appearing so user resizes within the same hover
  // are preserved; the default kicks in again on the next fresh hover.
  constexpr ImVec2 kPreviewDefaultSize(800.0f, 540.0f);
  // Dead-zone margin (px) so the cursor can travel from the source link to
  // the popup without dismissing the preview.
  constexpr float kFlightMargin = 12.0f;
  // Gap between the cursor and the popup (px) and the safety margin kept
  // between the popup and the viewport edge (px) when clamping.
  constexpr float kCursorGap = 18.0f;
  constexpr float kViewportMargin = 8.0f;

  g_hover_preview_drawn_frame = frame;
  g_rendering_hover_preview = true;
  const std::string previous_document_path = g_preview_document_path;
  std::string preview_markdown = preview.body;

  // Smart-position the popup so it stays inside the application viewport
  // even when the source link is near the bottom/right edge. The default
  // is below-right of the cursor; if that would clip, try above/left, and
  // finally clamp into the viewport work area.
  const ImVec2 viewport_pos = ImGui::GetMainViewport()->WorkPos;
  const ImVec2 viewport_size = ImGui::GetMainViewport()->WorkSize;
  const float vp_x = viewport_pos.x;
  const float vp_y = viewport_pos.y;
  const float vp_w = viewport_size.x;
  const float vp_h = viewport_size.y;

  ImVec2 desired_pos(preview.mouse_pos.x + kCursorGap, preview.mouse_pos.y + kCursorGap);
  const ImVec2 desired_size = kPreviewDefaultSize;

  // Vertical: prefer above the cursor when below would clip the bottom.
  if(desired_pos.y + desired_size.y > vp_y + vp_h - kViewportMargin)
  {
    const float above_y = preview.mouse_pos.y - kCursorGap - desired_size.y;
    if(above_y >= vp_y + kViewportMargin)
      desired_pos.y = above_y;
    else
      desired_pos.y = std::max(vp_y + kViewportMargin, vp_y + vp_h - desired_size.y - kViewportMargin);
  }

  // Horizontal: prefer left of the cursor when right would clip the edge.
  if(desired_pos.x + desired_size.x > vp_x + vp_w - kViewportMargin)
  {
    const float left_x = preview.mouse_pos.x - kCursorGap - desired_size.x;
    if(left_x >= vp_x + kViewportMargin)
      desired_pos.x = left_x;
    else
      desired_pos.x = std::max(vp_x + kViewportMargin, vp_x + vp_w - desired_size.x - kViewportMargin);
  }

  // Final clamp: never let the popup cross the top/left edges either.
  if(desired_pos.x < vp_x + kViewportMargin) desired_pos.x = vp_x + kViewportMargin;
  if(desired_pos.y < vp_y + kViewportMargin) desired_pos.y = vp_y + kViewportMargin;

  ImGui::SetNextWindowPos(desired_pos, ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(kPreviewDefaultSize, ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints(kPreviewMinSize, kPreviewMaxSize);

  MarkdownView::set_hover_preview_enabled(false);
  set_preview_document_path(preview.path);
  g_force_open_preview_headers = true;

  const std::string window_title = preview.title + "##link_preview";
  bool window_hovered = false;
  ImVec2 popup_pos(0.0f, 0.0f);
  ImVec2 popup_size(0.0f, 0.0f);
  MarkdownSupport::PreviewRenderResult popup_render_result;
  // Drop AlwaysAutoResize: a scrollable body needs a stable window size so
  // scrollbars can engage and the user can resize from the corner.
  if(ImGui::Begin(window_title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings))
  {
    popup_pos = ImGui::GetWindowPos();
    popup_size = ImGui::GetWindowSize();
    window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    // Make text wrap to the popup's content width, not the parent window's.
    const float popup_content_w = std::max(1.0f, popup_size.x - ImGui::GetStyle().WindowPadding.x * 2.0f);
    MarkdownView::set_render_width(popup_content_w);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + popup_content_w);

    // Scrollable body — wheel & scrollbar work as expected on overflow.
    MarkdownSupport::PreviewRenderResult popup_render_result;
    if(ImGui::BeginChild("##link_preview_body", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
      popup_render_result = render_preview_with_task_checkboxes_ex(preview_markdown);
    }
    ImGui::EndChild();

    ImGui::PopTextWrapPos();
  }
  ImGui::End();

  // Keep the preview alive when the cursor is still over the source link,
  // inside the popup, or in-flight between them.
  const ImVec2 mouse = ImGui::GetMousePos();
  const bool in_flight =
      popup_size.x > 0.0f && popup_size.y > 0.0f &&
      mouse.x >= popup_pos.x - kFlightMargin &&
      mouse.y >= popup_pos.y - kFlightMargin &&
      mouse.x <= popup_pos.x + popup_size.x + kFlightMargin &&
      mouse.y <= popup_pos.y + popup_size.y + kFlightMargin;
  if(!preview.link_hovered && !window_hovered && !in_flight) MarkdownView::clear_hover_preview();

  // Persist interactive edits (checkbox toggles, table edits, ...) to the
  // source file. Only writes when something actually changed and the section
  // range is known.
  if(popup_render_result.markdown_changed)
    persist_hover_preview_edit(preview, preview_markdown);

  g_force_open_preview_headers = false;
  set_preview_document_path(previous_document_path);
  MarkdownView::set_hover_preview_enabled(true);
  g_rendering_hover_preview = false;
}

std::string portable_document_key(std::string_view document_path)
{
  if(document_path.empty()) return "__active_note__";
  if(!g_preview_project_paths) return std::string(document_path);

  const std::filesystem::path path{std::string(document_path)};
  if(path.is_absolute())
  {
    if(auto key = g_preview_project_paths->stable_key(path)) return *key;
  }
  else if(auto decoded = g_preview_project_paths->decode(document_path))
  {
    if(auto key = g_preview_project_paths->stable_key(*decoded)) return *key;
  }
  return std::string(document_path);
}

std::string current_document_key()
{
  return g_preview_document_key.empty() ? std::string("__active_note__") : g_preview_document_key;
}

void ensure_preview_state_loaded()
{
  if(g_preview_state_loaded) return;
  g_preview_state_loaded = true;

  g_preview_state_json = Json::object();
  g_preview_state_json["documents"] = Json::object();
  const auto loaded = g_preview_files.load(g_preview_state_file);
  record_preview_read(g_preview_state_file, loaded);
  if(!loaded)
  {
    g_preview_state_loaded = false;
    return;
  }
  if(loaded.snapshot.existed)
  {
    g_preview_state_json = Json::parse(loaded.snapshot.content, nullptr, false);
    if(g_preview_state_json.is_discarded())
      g_preview_state_json = Json::object();
  }

  if(!g_preview_state_json.is_object()) g_preview_state_json = Json::object();
  if(!g_preview_state_json.contains("documents") || !g_preview_state_json["documents"].is_object())
    g_preview_state_json["documents"] = Json::object();

  if(!g_preview_project_root.empty())
  {
    const Json original_documents = g_preview_state_json["documents"];
    Json original_legacy_documents = g_preview_state_json.value("legacyDocuments", Json::object());
    if(!original_legacy_documents.is_object()) original_legacy_documents = Json::object();

    Json portable_documents = Json::object();
    Json remaining_legacy_documents = Json::object();
    notepp::project_paths::ProjectPaths paths(g_preview_project_root);
    auto portable_key_for = [&](const std::string &key) -> std::optional<std::string> {
      if(key == "__active_note__") return key;
      if(auto decoded = paths.decode(key))
      {
        if(auto stable = paths.stable_key(*decoded)) return *stable;
      }
      if(auto legacy = paths.migrate_legacy(key, g_preview_notes_top_level))
        return legacy->stored_path;
      return std::nullopt;
    };
    auto preserve_legacy = [&](const std::string &key, const Json &value) {
      std::string preserved_key = key;
      std::size_t collision = 2;
      while(remaining_legacy_documents.contains(preserved_key) &&
            remaining_legacy_documents[preserved_key] != value)
        preserved_key = key + "#collision-" + std::to_string(collision++);
      remaining_legacy_documents[preserved_key] = value;
    };
    auto migrate_entry = [&](const std::string &key, const Json &value) {
      const auto portable_key = portable_key_for(key);
      if(!portable_key)
      {
        preserve_legacy(key, value);
        return;
      }
      if(!portable_documents.contains(*portable_key))
      {
        portable_documents[*portable_key] = value;
        return;
      }
      if(portable_documents[*portable_key] != value)
        preserve_legacy(key, value);
    };

    for(const auto &[key, value] : original_documents.items())
      migrate_entry(key, value);
    // Retry previously quarantined keys on every load. Restoring a missing note
    // can make a legacy absolute key portable on a later launch.
    for(const auto &[key, value] : original_legacy_documents.items())
      migrate_entry(key, value);

    if(portable_documents != original_documents ||
       remaining_legacy_documents != original_legacy_documents)
    {
      g_preview_state_json["documents"] = std::move(portable_documents);
      g_preview_state_json["legacyDocuments"] = std::move(remaining_legacy_documents);
      g_preview_state_dirty = true;
    }
  }
}

std::string first_non_empty_filter(const Json &arr)
{
  if(!arr.is_array()) return {};
  for(const Json &v : arr)
  {
    if(!v.is_string()) continue;
    const std::string s = v.get<std::string>();
    if(!StringUtils::trim(s).empty()) return s;
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

bool save_preview_state_if_dirty()
{
  if(!g_preview_state_dirty) return true;
  ensure_preview_state_loaded();

  if(!save_preview_file(g_preview_state_file, g_preview_state_json.dump(2))) return false;
  g_preview_state_dirty = false;
  return true;
}

bool set_all_preview_headers_open_impl(std::string_view document_path, std::string_view markdown, bool open)
{
  const std::string doc_key = portable_document_key(document_path);
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
  const std::string doc_key = portable_document_key(document_path);
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

  Json ui_state = Json::parse(MarkdownWidgets::capture_ui_state_snapshot(), nullptr, false);
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
  MarkdownWidgets::apply_ui_state_snapshot(ui_state.dump());
  g_preview_state_dirty = true;
  save_preview_state_if_dirty();
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

  const std::string contains_lower = to_lower_ascii_copy(StringUtils::trim(state.contains_filter));
  const std::string not_contains_lower = to_lower_ascii_copy(StringUtils::trim(state.not_contains_filter));

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
    const std::string value = std::string(StringUtils::trim(g_filter_dialog_state.buffer));
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
    return !StringUtils::trim(state.contains_filter).empty() && state.contains_filter_column == column;
  };
  auto column_has_not_contains_filter = [&](int column) {
    return !StringUtils::trim(state.not_contains_filter).empty() && state.not_contains_filter_column == column;
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
        ImGui::InvisibleButton("##cell_hitbox", nonzero_invisible_button_size(hitbox_width, hitbox_height));
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
            !StringUtils::trim(state.contains_filter).empty() ||
            !StringUtils::trim(state.not_contains_filter).empty();
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
  if(path == g_preview_document_path) return;

  if(!path.empty())
  {
    const std::filesystem::path document_path{std::string(path)};
    const auto loaded = g_preview_files.ensure_loaded(document_path);
    const std::string key = document_path.lexically_normal().generic_string();
    if(!loaded)
      g_preview_persistence_errors[key] = loaded.message;
  }
  g_preview_document_path.assign(path.data(), path.size());
  g_preview_document_key = portable_document_key(path);
  MarkdownView::set_document_path(path);
  MarkdownWidgets::set_widget_document_path(std::filesystem::path(path));
}

void notify_document_moved(const std::filesystem::path &from,
                           const std::filesystem::path &to)
{
  MarkdownWidgets::notify_document_moved(from, to);
  atomic_file::move_path_value(g_preview_persistence_errors, from, to);
  g_preview_persistence_guard.moved(from, to);
}

void notify_document_saved(const std::filesystem::path &path)
{
  MarkdownWidgets::notify_document_saved(path);
  g_preview_persistence_guard.forget(path);
  const auto loaded = g_preview_files.load(path);
  record_preview_read(path, loaded);
}

void set_preview_state_path(const std::filesystem::path &path)
{
  g_preview_files.clear();
  g_preview_persistence_guard.clear();
  g_preview_persistence_errors.clear();
  g_preview_state_file = path.string();
  g_preview_project_root = path.parent_path().parent_path();
  g_preview_project_paths.emplace(g_preview_project_root);
  std::error_code path_error;
  if(std::filesystem::is_directory(g_preview_project_root / "notes", path_error) && !path_error)
    g_preview_notes_top_level = "notes";
  else
    g_preview_notes_top_level = "data";
  g_preview_state_json = Json::object();
  g_preview_state_loaded = false;
  g_preview_state_dirty = false;
  g_table_state_cache.clear();
  g_preview_document_key = portable_document_key(g_preview_document_path);
}

bool flush_preview_state()
{
  return save_preview_state_if_dirty();
}

std::string last_persistence_error()
{
  std::string combined;
  for(const auto &[path, error] : g_preview_persistence_errors)
  {
    if(!combined.empty()) combined += "\n";
    combined += path + ": " + error;
  }
  return combined;
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
    // True when no real ImGui::TreeNode was pushed for this header
    // (used in the hover preview, which renders headings as non-collapsible
    // text). Prevents a spurious ImGui::TreePop on cleanup.
    bool skip_pop = false;
  };

  std::vector<HeaderUi> header_stack;

  auto flush_chunk = [&]() {
    if(normal_chunk.empty()) return;

    // Do not cull a whole markdown chunk based only on its starting Y position.
    // Large notes may have a chunk whose start has scrolled above the viewport
    // while later content is still visible; replacing it with a Dummy makes the
    // preview appear blank. Proper virtualization needs block-level measured
    // heights, not coarse chunk-start culling.
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
    const std::string_view tline = StringUtils::trim(line);

    int heading_level = 0;
    std::string_view heading_title;
    if(parse_heading_line(line, heading_level, heading_title))
    {
      flush_chunk();
      while(!header_stack.empty() && header_stack.back().level >= heading_level)
      {
        if(header_stack.back().open && !header_stack.back().skip_pop) ImGui::TreePop();
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
      if(g_force_open_preview_headers)
      {
        // Hover preview: render the heading as a non-collapsible line and
        // never let the user hide the content under it. We push a sentinel
        // onto header_stack so the rest of the bookkeeping (and the cleanup
        // loop at the end) keeps working without a real TreeNode.
        ImGui::Text("%s", std::string(heading_title).c_str());
        header_stack.push_back(HeaderUi{heading_level, true, true /* skip_pop */});
        pos = has_newline ? line_end + 1 : line_end;
        continue;
      }
      if(has_saved_open)
        ImGui::SetNextItemOpen(saved_open, ImGuiCond_Always);
      const bool open = ImGui::TreeNodeEx(
          reinterpret_cast<void *>(static_cast<intptr_t>(static_cast<int>(line_start) + 0x10000)),
          ImGuiTreeNodeFlags_SpanAvailWidth,
          "%s",
          std::string(heading_title).c_str());
      if(!has_saved_open || saved_open != open)
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

    if(tline == "```ui")
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
        if(StringUtils::trim(l) == "```")
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
        const MarkdownWidgets::RenderResult ui_result = MarkdownWidgets::try_render_ui_block(markdown, line_start, line_end, block_end);
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
        if(StringUtils::trim(l) == "```")
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
        const std::string resolved = MarkdownWidgets::resolve_ui_mermaid_template(markdown, body);
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
        if(StringUtils::trim(l) == "```")
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
          const std::string_view tl = StringUtils::trim(l);

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
      ImGui::NewLine();
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
    if(header_stack.back().open && !header_stack.back().skip_pop) ImGui::TreePop();
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
  result.markdown_changed = result.markdown_changed || img_ctx.markdown_changed;

  // Consume right-click if a diagram handled it (prevents the note "Copy all" popup)
  if(MermaidDiagrams::g_consumed_right_click)
  {
    result.consumed_right_click = true;
    MermaidDiagrams::g_consumed_right_click = false;
  }

  // Apply any interactive diagram edit committed this frame.
  //
  // Diagrams are rendered through two paths in this dispatcher:
  //   1. An explicit fence (`` ```mermaid `` / `` ```ui-mermaid `` /
  //      `` ```ui ``) — the body is captured between the fence and the
  //      next `` ``` `` line. `pe.id` points at the fence line.
  //   2. An implicit block, where the parser just discovered a line
  //      whose first token is a known mermaid type (e.g. ``kanban``)
  //      and consumed it without any fence — `pe.id` points at that
  //      first line of the body itself.
  //
  // For case 1 the body starts one line past the fence. For case 2 the
  // body starts AT `pe.id`, otherwise the old diagram header line gets
  // left in place before the serialised replacement and we end up with
  // two `kanban` headers stacked on top of each other (which the next
  // re-render then collapses into a single "kanban" column holding all
  // cards, visibly corrupting the board).
  if(MermaidDiagrams::g_pending_edit.active())
  {
    auto &pe = MermaidDiagrams::g_pending_edit;
    const size_t fence_start = static_cast<size_t>(pe.id);
    if(fence_start < markdown.size())
    {
      const size_t fence_line_end = markdown.find('\n', fence_start);
      const std::string_view fence_first_line(
          markdown.data() + fence_start,
          fence_line_end == std::string_view::npos
              ? markdown.size() - fence_start
              : fence_line_end - fence_start);
      const std::string_view fence_first_trim = StringUtils::trim(fence_first_line);
      const bool explicit_fence =
          fence_first_trim == "```mermaid" ||
          fence_first_trim == "```ui-mermaid" ||
          fence_first_trim == "```ui";

      const size_t body_start =
          (explicit_fence && fence_line_end != std::string_view::npos)
              ? fence_line_end + 1
              : fence_start;

      if(body_start < markdown.size())
      {
        size_t body_end = body_start;
        while(body_end < markdown.size())
        {
          if(markdown[body_end] == '`' &&
             body_end + 2 < markdown.size() &&
             markdown[body_end + 1] == '`' &&
             markdown[body_end + 2] == '`')
            break;
          size_t nl = markdown.find('\n', body_end);
          body_end = (nl == std::string::npos) ? markdown.size() : nl + 1;
        }
        std::string nb = pe.body;
        if(!nb.empty() && nb.back() != '\n') nb += '\n';
        markdown.replace(body_start, body_end - body_start, nb);
        result.markdown_changed = true;
      }
    }
    pe.clear();
  }

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
