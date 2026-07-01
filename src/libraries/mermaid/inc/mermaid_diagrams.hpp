#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MermaidDiagrams
{

// ── Common parser result type ─────────────────────────────────────────────
//
// All diagram parsers return the same shape: a parsed value (valid only when
// the result is `ok()`) plus a diagnostic on failure. The legacy `bool
// parse_X(...)` functions are kept as thin compatibility wrappers around
// these new API entries.
template <typename Diagram>
struct ParseResult
{
    bool        success = false;
    Diagram     value{};
    std::string error; // Empty when ok.

    bool ok() const noexcept { return success; }
};

// Convenience constructor for successful parses.
template <typename Diagram>
inline ParseResult<Diagram> make_parsed(Diagram value)
{
    ParseResult<Diagram> r;
    r.success = true;
    r.value = std::move(value);
    return r;
}

// Convenience constructor for failed parses.
template <typename Diagram>
inline ParseResult<Diagram> make_parse_error(std::string message)
{
    ParseResult<Diagram> r;
    r.success = false;
    r.error = std::move(message);
    return r;
}


// ── Sequence Diagram ─────────────────────────────────────────────────────────
struct SeqParticipant { std::string id, label; bool is_actor = false; };
struct SeqMessage {
  std::string from, to, text;
  bool dotted = false;
  bool open   = false;
  bool bidir  = false;
};
struct SeqNote { std::string over1, over2, text; };  // over2 nonempty → range
struct SeqGroup { std::string type, label; int start_event = 0, end_event = 0; };
struct SequenceDiagram {
  std::string title;
  std::vector<SeqParticipant> participants;
  struct Event {
    enum class T { Message, Note, Activate, Deactivate, GroupStart, GroupEnd } type;
    int idx = -1;           // index into messages / notes
    std::string label;      // for GroupStart label
    std::string group_type; // loop / alt / opt / par / break
    std::string actor_id;   // for Activate / Deactivate
  };
  std::vector<Event>      events;
  std::vector<SeqMessage> messages;
  std::vector<SeqNote>    notes;
};
bool parse_sequence(std::string_view src, SequenceDiagram &out);
void render_sequence(const SequenceDiagram &d, int id);

// ── Class Diagram ────────────────────────────────────────────────────────────
struct ClassMember { char vis = '+'; std::string type, name; bool is_method = false; };
struct ClassDef { std::string name, stereotype; std::vector<ClassMember> members; };
struct ClassRel {
  std::string from, to, label, from_card, to_card;
  enum class T { Inheritance, Composition, Aggregation, Association, Dependency,
                 Realization, Link } type = T::Link;
};
struct ClassDiagram { std::vector<ClassDef> classes; std::vector<ClassRel> relations; };
bool parse_class(std::string_view src, ClassDiagram &out);
void render_class(const ClassDiagram &d, int id);

// ── State Diagram ────────────────────────────────────────────────────────────
struct StateNode { std::string id, label; bool is_start = false, is_end = false; };
struct StateTrans { std::string from, to, label; };
struct StateDiagram { std::vector<StateNode> states; std::vector<StateTrans> transitions; };
bool parse_state(std::string_view src, StateDiagram &out);
void render_state(const StateDiagram &d, int id);

// ── ER Diagram ───────────────────────────────────────────────────────────────
struct ERAttr { std::string type, name; bool pk = false, fk = false; };
struct EREntity { std::string name; std::vector<ERAttr> attrs; };
struct ERRel { std::string e1, e2, label, card1, card2; };
struct ERDiagram { std::vector<EREntity> entities; std::vector<ERRel> relations; };
bool parse_er(std::string_view src, ERDiagram &out);
void render_er(const ERDiagram &d, int id);

// ── User Journey ─────────────────────────────────────────────────────────────
struct JourneyTask { std::string name; int score = 3; std::vector<std::string> actors; };
struct JourneySection { std::string name; std::vector<JourneyTask> tasks; };
struct JourneyDiagram { std::string title; std::vector<JourneySection> sections; };
bool parse_journey(std::string_view src, JourneyDiagram &out);
void render_journey(const JourneyDiagram &d, int id);

// ── Gantt ────────────────────────────────────────────────────────────────────
struct GanttTask { std::string id, name, after; int start_day = 0, dur = 1;
                   bool is_milestone = false, is_crit = false; };
struct GanttSection { std::string name; std::vector<GanttTask> tasks; };
struct GanttDiagram { std::string title; std::vector<GanttSection> sections; };
bool parse_gantt(std::string_view src, GanttDiagram &out);
void render_gantt(const GanttDiagram &d, int id);

// ── Quadrant Chart ───────────────────────────────────────────────────────────
struct QuadPoint { std::string name; float x = 0.5f, y = 0.5f; };
struct QuadrantDiagram {
  std::string title, x_low, x_high, y_low, y_high, q1, q2, q3, q4;
  std::vector<QuadPoint> points;
};
bool parse_quadrant(std::string_view src, QuadrantDiagram &out);
void render_quadrant(const QuadrantDiagram &d, int id);

// ── Requirement Diagram ──────────────────────────────────────────────────────
struct Requirement { std::string type, name, id, text, risk, method; };
struct ReqElement  { std::string name, type, docref; };
struct ReqRel      { std::string from, to, reltype; };
struct RequirementDiagram {
  std::vector<Requirement> reqs;
  std::vector<ReqElement>  elements;
  std::vector<ReqRel>      relations;
};
bool parse_requirement(std::string_view src, RequirementDiagram &out);
void render_requirement(const RequirementDiagram &d, int id);

// ── Git Graph ────────────────────────────────────────────────────────────────
struct GitCommit { std::string id, tag, branch, merge_from;
                   enum class T { Normal, Reverse, Highlight } type = T::Normal;
                   bool is_merge = false; };
struct GitDiagram { std::vector<std::string> branches; std::vector<GitCommit> commits;
                    std::string main_branch; };
bool parse_git(std::string_view src, GitDiagram &out);
void render_git(const GitDiagram &d, int id);

// ── Mindmap ──────────────────────────────────────────────────────────────────
struct MindNode { std::string label; int level = 0, parent = -1;
                  std::vector<int> children; };
struct MindmapDiagram { std::vector<MindNode> nodes; };
bool parse_mindmap(std::string_view src, MindmapDiagram &out);
void render_mindmap(const MindmapDiagram &d, int id);

// ── Timeline ─────────────────────────────────────────────────────────────────
struct TLPeriod { std::string label; std::vector<std::string> events; };
struct TimelineDiagram { std::string title; std::vector<TLPeriod> periods; };
bool parse_timeline(std::string_view src, TimelineDiagram &out);
void render_timeline(const TimelineDiagram &d, int id);

// ── Sankey ───────────────────────────────────────────────────────────────────
struct SankeyFlow { std::string source, target; float value = 0.0f; };
struct SankeyDiagram { std::vector<SankeyFlow> flows; };
bool parse_sankey(std::string_view src, SankeyDiagram &out);
void render_sankey(const SankeyDiagram &d, int id);

// ── XY Chart ─────────────────────────────────────────────────────────────────
struct XYAxisConfig {
  bool show_label = true;
  bool show_title = true;
  bool show_tick = true;
  bool show_axis_line = true;
  float label_padding = 5.0f;
  float title_padding = 5.0f;
  float tick_length = 5.0f;
  float tick_width = 2.0f;
  float axis_line_width = 2.0f;
};
struct XYChartConfig {
  float width = 700.0f;
  float height = 500.0f;
  bool show_title = true;
  bool show_data_label = false;
  bool show_data_label_outside_bar = false;
  float title_padding = 10.0f;
  float plot_reserved_space_percent = 50.0f;
  XYAxisConfig x_axis;
  XYAxisConfig y_axis;
};
struct XYSeries { std::string label; bool is_bar = true; std::vector<float> data; };
struct XYDiagram {
  std::string title;
  std::string x_title;
  std::string y_title;
  std::vector<std::string> x_labels;
  float y_min = 0.0f, y_max = 1.0f;
  bool y_explicit = false;
  std::vector<XYSeries> series;
  bool horizontal = false;
  XYChartConfig config;
};
bool parse_xychart(std::string_view src, XYDiagram &out);
void render_xychart(const XYDiagram &d, int id);

// ── Block Diagram ────────────────────────────────────────────────────────────
struct BlockNode { std::string id, label, shape; };
struct BlockEdge { std::string from, to, label; };
struct BlockDiagram { int columns = 1; std::vector<BlockNode> nodes; std::vector<BlockEdge> edges; };
bool parse_block(std::string_view src, BlockDiagram &out);
void render_block(const BlockDiagram &d, int id);

// ── Packet ───────────────────────────────────────────────────────────────────
struct PacketField  { int start = 0, end = 0; std::string name; };
struct PacketConfig {
    float bitWidth   = 20.0f; // pixels per bit  (mermaid: bitWidth)
    float rowHeight  = 40.0f; // field row height (mermaid: rowHeight)
    int   bitsPerRow = 32;    // bits shown per row (mermaid: bitsPerRow)
    bool  showBits   = true;   // display bit-number header (mermaid: showBits)
    float paddingX   = 8.0f;  // horizontal outer padding (mermaid: paddingX)
    float paddingY   = 6.0f;  // vertical padding between rows (mermaid: paddingY)
    bool  showLegend = false;  // show legend strip for fields too narrow for inline labels
};
struct PacketDiagram { std::string title; std::vector<PacketField> fields; int total_bits = 0; PacketConfig config; };
bool parse_packet(std::string_view src, PacketDiagram &out);
void render_packet(const PacketDiagram &d, int id);

// ── Kanban ───────────────────────────────────────────────────────────────────
struct KanbanCard { std::string id, label, description; };
struct KanbanCol  { std::string id, label; std::vector<KanbanCard> cards; };
struct KanbanDiagram { std::vector<KanbanCol> columns; };
bool parse_kanban(std::string_view src, KanbanDiagram &out);
void render_kanban(const KanbanDiagram &d, int id);

// ── Architecture ─────────────────────────────────────────────────────────────
struct ArchService { std::string id, icon, label, group; };
struct ArchGroup   { std::string id, icon, label; };
struct ArchEdge    { std::string from, to; };
struct ArchDiagram { std::vector<ArchGroup> groups; std::vector<ArchService> services;
                     std::vector<ArchEdge> edges; };
bool parse_architecture(std::string_view src, ArchDiagram &out);
void render_architecture(const ArchDiagram &d, int id);

// ── Radar Chart ──────────────────────────────────────────────────────────────
struct RadarCurve { std::string name; std::vector<float> values; };
struct RadarDiagram { std::string title; std::vector<std::string> axes;
                      std::vector<RadarCurve> curves; float max_val = 100.0f; };
bool parse_radar(std::string_view src, RadarDiagram &out);
void render_radar(const RadarDiagram &d, int id);

// ── Treemap ──────────────────────────────────────────────────────────────────
struct TreemapNode { std::string name; float value = 0.0f; int parent = -1;
                     std::vector<int> children; };
struct TreemapDiagram { std::vector<TreemapNode> nodes; };
bool parse_treemap(std::string_view src, TreemapDiagram &out);
void render_treemap(const TreemapDiagram &d, int id);

// ── ZenUML ───────────────────────────────────────────────────────────────────
bool parse_zenuml(std::string_view src, SequenceDiagram &out);
void render_zenuml(const SequenceDiagram &d, int id);

// ── Event Modeling ───────────────────────────────────────────────────────────
struct EMItem { enum class T { Command, Event, ReadModel, Policy, Processor } type;
                std::string name; };
struct EMLink { std::string from, to; };
struct EventModelingDiagram { std::string title; std::vector<EMItem> items;
                               std::vector<EMLink> links; };
bool parse_eventmodeling(std::string_view src, EventModelingDiagram &out);
void render_eventmodeling(const EventModelingDiagram &d, int id);

// ── Venn Diagram ─────────────────────────────────────────────────────────────
struct VennSet          { std::string id, label; };
struct VennIntersection { std::string label; std::vector<std::string> set_ids; };
struct VennDiagram { std::string title; std::vector<VennSet> sets;
                     std::vector<VennIntersection> intersections; };
bool parse_venn(std::string_view src, VennDiagram &out);
void render_venn(const VennDiagram &d, int id);

// ── Ishikawa (Fishbone) ──────────────────────────────────────────────────────
struct IshikawaCause    { std::string text; std::vector<IshikawaCause> sub; };
struct IshikawaCategory { std::string name; std::vector<IshikawaCause> causes; };
struct IshikawaDiagram  { std::string effect; std::vector<IshikawaCategory> categories; };
bool parse_ishikawa(std::string_view src, IshikawaDiagram &out);
void render_ishikawa(const IshikawaDiagram &d, int id);

// ── Wardley Map ──────────────────────────────────────────────────────────────
struct WardleyComp { std::string name; float visibility = 0.5f, evolution = 0.5f; };
struct WardleyLink { std::string from, to; };
struct WardleyDiagram { std::string title; std::vector<WardleyComp> components;
                        std::vector<WardleyLink> links; };
bool parse_wardley(std::string_view src, WardleyDiagram &out);
void render_wardley(const WardleyDiagram &d, int id);

// ── TreeView ─────────────────────────────────────────────────────────────────
struct TVNode { std::string label; int parent = -1; std::vector<int> children; };
struct TreeViewDiagram { std::vector<TVNode> nodes; };
bool parse_treeview(std::string_view src, TreeViewDiagram &out);
void render_treeview(const TreeViewDiagram &d, int id);

// ── Interactive edit back-channel ─────────────────────────────────────────
// render_* functions write here when the user edits a diagram interactively.
// render_preview_with_task_checkboxes_ex reads and applies it to the markdown.
struct PendingEdit {
    int         id = -1;   // line_start byte offset (same 'id' passed to render_*)
    std::string body;      // new mermaid block body to replace the old one
    bool active() const { return id >= 0; }
    void clear()        { id = -1; body.clear(); }
};
extern PendingEdit g_pending_edit;
// Set by render_* when it handles a right-click; read + cleared by markdown_support
// so the note-level "Copy all" popup does not open on the same click.
extern bool g_consumed_right_click;

// ── Render registry ──────────────────────────────────────────────────────────
//
// The registry maps a canonical diagram type (e.g. "sequencediagram") to its
// parser, renderer, and accepted aliases (e.g. {"statediagram",
// "statediagram-v2"}). It lets callers dispatch a diagram body without a
// long if/else chain and makes it easy to add a new diagram type without
// touching the call site.
//
// The parser/renderer function pointers are typed using a void* indirection
// to keep the registry uniform across diagrams with different value types.
// The caller is expected to know the diagram type and cast the diagram value
// accordingly. For the common case, prefer the higher-level dispatch
// function below which encapsulates the per-type cast.
struct RegistryEntry
{
    const char *      canonical;     // canonical type name (e.g. "sequencediagram")
    bool (*parse_any)(std::string_view, void *);  // writes parsed diagram into *out
    void (*render_any)(const void *, int);        // renders the parsed diagram
};

// Look up a registry entry for a diagram type (case-insensitive, accepts
// aliases). Returns nullptr if the type is not registered.
const RegistryEntry *find_registry_entry(std::string_view mermaid_type);

// Return true when the type is known to the registry.
bool is_registered_type(std::string_view mermaid_type);

// Returns the number of registered diagram types. Useful for tests.
std::size_t registered_type_count();

} // namespace MermaidDiagrams
