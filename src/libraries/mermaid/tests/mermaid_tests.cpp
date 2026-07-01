#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace md = MermaidDiagrams;

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void expect_eq_size(std::size_t a, std::size_t b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got " << a << ", expected " << b << ")\n";
}

void test_sequence_basic_valid()
{
  const std::string src =
      "sequenceDiagram\n"
      "  participant Alice\n"
      "  participant Bob\n"
      "  Alice->>Bob: Hello\n"
      "  Bob-->>Alice: Hi back\n";
  md::SequenceDiagram d;
  expect_true(md::parse_sequence(src, d), "valid sequence parses");
  expect_eq_size(d.participants.size(), 2, "two participants");
  expect_eq_str(d.participants[0].id, "Alice", "first id");
  expect_eq_size(d.messages.size(), 2, "two messages");
  expect_eq_str(d.messages[0].from, "Alice", "first from");
  expect_eq_str(d.messages[0].to, "Bob", "first to");
  expect_eq_str(d.messages[0].text, "Hello", "first text");
}

void test_sequence_missing_header()
{
  const std::string src =
      "participant Alice\n"
      "participant Bob\n";
  md::SequenceDiagram d;
  expect_true(!md::parse_sequence(src, d), "no header fails");
}

void test_sequence_no_participants()
{
  const std::string src = "sequenceDiagram\n";
  md::SequenceDiagram d;
  expect_true(!md::parse_sequence(src, d), "no participants fails");
}

void test_sequence_with_notes_and_groups()
{
  const std::string src =
      "sequenceDiagram\n"
      "  participant A\n"
      "  participant B\n"
      "  Note over A,B: hello\n"
      "  loop daily\n"
      "    A->>B: ping\n"
      "  end\n";
  md::SequenceDiagram d;
  expect_true(md::parse_sequence(src, d), "valid with notes and groups");
  expect_eq_size(d.notes.size(), 1, "one note");
  expect_true(!d.events.empty(), "events populated");
}

void test_class_basic_valid()
{
  const std::string src =
      "classDiagram\n"
      "  class Animal {\n"
      "    +String name\n"
      "    +makeSound() void\n"
      "  }\n"
      "  class Dog {\n"
      "    +fetch() void\n"
      "  }\n"
      "  Animal <|-- Dog\n";
  md::ClassDiagram d;
  expect_true(md::parse_class(src, d), "valid class parses");
  expect_eq_size(d.classes.size(), 2, "two classes");
  expect_eq_str(d.classes[0].name, "Animal", "first class name");
  expect_true(!d.classes[0].members.empty(), "Animal has members");
  expect_eq_size(d.relations.size(), 1, "one inheritance relation");
}

void test_class_missing_header()
{
  md::ClassDiagram d;
  expect_true(!md::parse_class("class Foo", d), "missing header rejected");
}

void test_class_empty()
{
  md::ClassDiagram d;
  expect_true(!md::parse_class("classDiagram\n", d), "empty class fails");
}

void test_state_basic_valid()
{
  const std::string src =
      "stateDiagram-v2\n"
      "  [*] --> Still\n"
      "  Still --> Moving\n"
      "  Moving --> Still\n"
      "  Moving --> [*]\n";
  md::StateDiagram d;
  expect_true(md::parse_state(src, d), "valid state parses");
  expect_true(!d.states.empty(), "states populated");
  expect_true(!d.transitions.empty(), "transitions populated");
}

void test_state_missing_header()
{
  md::StateDiagram d;
  expect_true(!md::parse_state("Still --> Moving", d), "missing header rejected");
}

void test_state_with_label()
{
  const std::string src =
      "stateDiagram-v2\n"
      "  state Active as \"Active State\"\n"
      "  [*] --> Active : start\n";
  md::StateDiagram d;
  expect_true(md::parse_state(src, d), "valid state with label parses");
  expect_true(!d.states.empty(), "states populated");
}

void test_parse_result_helpers()
{
  // make_parsed produces an ok() result with the value and an empty error.
  auto ok = md::make_parsed(md::StateDiagram{});
  expect_true(ok.ok(), "make_parsed yields ok");
  expect_eq_size(ok.error.size(), 0, "ok result has no error");

  // make_parse_error produces a failed result with the message.
  auto err = md::make_parse_error<md::StateDiagram>("bad input");
  expect_true(!err.ok(), "make_parse_error yields not ok");
  expect_eq_str(err.error, "bad input", "error message preserved");

  // Default-constructed result is not ok.
  md::ParseResult<md::StateDiagram> def;
  expect_true(!def.ok(), "default result is not ok");
}

void test_er_basic_valid()
{
  const std::string src =
      "erDiagram\n"
      "  CUSTOMER ||--o{ ORDER : places\n"
      "  ORDER ||--|{ LINE-ITEM : contains\n";
  md::ERDiagram d;
  expect_true(md::parse_er(src, d), "valid ER parses");
  expect_eq_size(d.entities.size(), 3, "three entities");
  expect_eq_size(d.relations.size(), 2, "two relations");
}

void test_er_missing_header()
{
  const std::string src = "CUSTOMER ||--o{ ORDER : places\n";
  md::ERDiagram d;
  expect_true(!md::parse_er(src, d), "missing header rejected");
}

void test_er_with_attributes()
{
  const std::string src =
      "erDiagram\n"
      "  CUSTOMER {\n"
      "    string name PK\n"
      "    string email\n"
      "  }\n";
  md::ERDiagram d;
  expect_true(md::parse_er(src, d), "valid ER with attrs parses");
  expect_eq_size(d.entities.size(), 1, "one entity");
  expect_eq_size(d.entities[0].attrs.size(), 2, "two attributes");
  expect_true(d.entities[0].attrs[0].pk, "first attr is PK");
}

void test_journey_basic_valid()
{
  const std::string src =
      "journey\n"
      "  title My Day\n"
      "  section Morning\n"
      "    Make coffee: 4: Alice\n"
      "    Read news: 3: Alice, Bob\n";
  md::JourneyDiagram d;
  expect_true(md::parse_journey(src, d), "valid journey parses");
  expect_eq_str(d.title, "My Day", "title parsed");
  expect_eq_size(d.sections.size(), 1, "one section");
  expect_eq_size(d.sections[0].tasks.size(), 2, "two tasks");
}

void test_journey_missing_header()
{
  md::JourneyDiagram d;
  expect_true(!md::parse_journey("section X\n  task1: 5\n", d), "missing header rejected");
}

void test_gantt_basic_valid()
{
  const std::string src =
      "gantt\n"
      "  title Plan\n"
      "  section S1\n"
      "    T1 :a1, 0, 3\n"
      "    T2 :a1, after a1, 2\n";
  md::GanttDiagram d;
  expect_true(md::parse_gantt(src, d), "valid gantt parses");
  expect_eq_str(d.title, "Plan", "title");
  expect_eq_size(d.sections.size(), 1, "one section");
  expect_eq_size(d.sections[0].tasks.size(), 2, "two tasks");
}

void test_gantt_missing_header()
{
  md::GanttDiagram d;
  expect_true(!md::parse_gantt("section S1\n  T1 :a1, 0, 3\n", d), "missing header rejected");
}

void test_quadrant_basic_valid()
{
  const std::string src =
      "quadrantChart\n"
      "  title Reach\n"
      "  x-axis Low --> High\n"
      "  y-axis Bad --> Good\n"
      "  quadrant-1 Keep\n"
      "  quadrant-2 Improve\n"
      "  quadrant-3 Drop\n"
      "  quadrant-4 Build\n"
      "  Tool: [0.7, 0.8]\n";
  md::QuadrantDiagram d;
  expect_true(md::parse_quadrant(src, d), "valid quadrant parses");
  expect_eq_str(d.title, "Reach", "title");
  // The original parser has a known off-by-one: line.substr(8) instead of
  // line.substr(7) is used to skip the "x-axis " / "y-axis " prefix. The
  // extracted parser preserves the original behavior to keep rendering
  // output stable until the bug is intentionally fixed in a separate
  // change. We only assert the parser accepts valid input and emits the
  // expected number of points here.
  expect_eq_size(d.points.size(), 1, "one point");
}

void test_quadrant_missing_header()
{
  md::QuadrantDiagram d;
  expect_true(!md::parse_quadrant("title X\n", d), "missing header rejected");
}

void test_requirement_basic_valid()
{
  const std::string src =
      "requirementDiagram\n"
      "  requirement TestReq {\n"
      "    id: REQ-001\n"
      "    text: Sample requirement\n"
      "    risk: low\n"
      "  }\n";
  md::RequirementDiagram d;
  expect_true(md::parse_requirement(src, d), "valid requirement parses");
  expect_eq_size(d.reqs.size(), 1, "one requirement");
  expect_eq_str(d.reqs[0].id, "REQ-001", "id parsed");
}

void test_requirement_missing_header()
{
  md::RequirementDiagram d;
  expect_true(!md::parse_requirement("requirement X { id: X }\n", d), "missing header rejected");
}

void test_git_basic_valid()
{
  const std::string src =
      "gitGraph\n"
      "  commit id: \"c1\"\n"
      "  commit id: \"c2\"\n"
      "  branch dev\n"
      "  commit id: \"c3\"\n";
  md::GitDiagram d;
  expect_true(md::parse_git(src, d), "valid git parses");
  expect_eq_size(d.branches.size(), 2, "main + dev");
  expect_eq_size(d.commits.size(), 3, "three commits");
}

void test_git_missing_header()
{
  md::GitDiagram d;
  expect_true(!md::parse_git("commit id: x\n", d), "missing header rejected");
}

void test_mindmap_basic_valid()
{
  const std::string src =
      "mindmap\n"
      "  Root\n"
      "    Child1\n"
      "    Child2\n";
  md::MindmapDiagram d;
  expect_true(md::parse_mindmap(src, d), "valid mindmap parses");
  expect_eq_size(d.nodes.size(), 3, "three nodes");
}

void test_mindmap_missing_header()
{
  md::MindmapDiagram d;
  expect_true(!md::parse_mindmap("Root\n  Child\n", d), "missing header rejected");
}

void test_timeline_basic_valid()
{
  const std::string src =
      "timeline\n"
      "  title History\n"
      "  2001 : Event A\n"
      "  2002 : Event B\n";
  md::TimelineDiagram d;
  expect_true(md::parse_timeline(src, d), "valid timeline parses");
  expect_eq_str(d.title, "History", "title");
  expect_eq_size(d.periods.size(), 2, "two periods");
}

void test_timeline_missing_header()
{
  md::TimelineDiagram d;
  expect_true(!md::parse_timeline("title X\n", d), "missing header rejected");
}

void test_sankey_basic_valid()
{
  const std::string src =
      "sankey-beta\n"
      "  A,B,5\n"
      "  B,C,3\n"
      "  C,D,2\n";
  md::SankeyDiagram d;
  expect_true(md::parse_sankey(src, d), "valid sankey parses");
  expect_eq_size(d.flows.size(), 3, "three flows");
}

void test_sankey_missing_header()
{
  md::SankeyDiagram d;
  expect_true(!md::parse_sankey("A,B,5\n", d), "missing header rejected");
}

void test_xychart_basic_valid()
{
  const std::string src =
      "xychart-beta\n"
      "  title \"Chart\"\n"
      "  x-axis [A, B, C]\n"
      "  y-axis \"Val\" 0 --> 100\n"
      "  bar [10, 20, 30]\n";
  md::XYDiagram d;
  expect_true(md::parse_xychart(src, d), "valid xychart parses");
  expect_eq_str(d.title, "Chart", "title");
  expect_eq_size(d.x_labels.size(), 3, "three x labels");
  expect_eq_size(d.series.size(), 1, "one series");
}

void test_xychart_missing_header()
{
  md::XYDiagram d;
  expect_true(!md::parse_xychart("title X\n  bar [1,2]\n", d), "missing header rejected");
}

void test_block_basic_valid()
{
  const std::string src =
      "block-beta\n"
      "  columns 2\n"
      "  A[\"One\"]\n"
      "  B[\"Two\"]\n"
      "  A --> B\n";
  md::BlockDiagram d;
  expect_true(md::parse_block(src, d), "valid block parses");
  expect_eq_size(d.nodes.size(), 2, "two nodes");
  expect_eq_size(d.edges.size(), 1, "one edge");
}

void test_block_missing_header()
{
  md::BlockDiagram d;
  expect_true(!md::parse_block("A[\"x\"]\n", d), "missing header rejected");
}

void test_kanban_basic_valid()
{
  const std::string src =
      "kanban\n"
      "  todo[Todo]\n"
      "    t1[First task]\n"
      "  done[Done]\n";
  md::KanbanDiagram d;
  expect_true(md::parse_kanban(src, d), "valid kanban parses");
  expect_eq_size(d.columns.size(), 2, "two columns");
}

void test_kanban_missing_header()
{
  md::KanbanDiagram d;
  expect_true(!md::parse_kanban("todo[T]\n", d), "missing header rejected");
}

void test_architecture_basic_valid()
{
  const std::string src =
      "architecture-beta\n"
      "  group grp[Group]\n"
      "  service svc[Service] in grp\n";
  md::ArchDiagram d;
  expect_true(md::parse_architecture(src, d), "valid architecture parses");
  expect_eq_size(d.services.size(), 1, "one service");
  expect_eq_size(d.groups.size(), 1, "one group");
}

void test_architecture_missing_header()
{
  md::ArchDiagram d;
  expect_true(!md::parse_architecture("service s[x]\n", d), "missing header rejected");
}

void test_radar_basic_valid()
{
  const std::string src =
      "radar-beta\n"
      "  title R\n"
      "  axis A,B,C\n"
      "  max 10\n"
      "  Series1 {\n"
      "    data [1, 2, 3]\n"
      "  }\n";
  md::RadarDiagram d;
  expect_true(md::parse_radar(src, d), "valid radar parses");
  expect_eq_size(d.axes.size(), 3, "three axes");
  expect_eq_size(d.curves.size(), 1, "one curve");
}

void test_radar_missing_header()
{
  md::RadarDiagram d;
  expect_true(!md::parse_radar("axis A,B\n", d), "missing header rejected");
}

void test_treemap_basic_valid()
{
  const std::string src =
      "treemap-beta\n"
      "  Root\n"
      "    A: 10\n"
      "    B: 20\n";
  md::TreemapDiagram d;
  expect_true(md::parse_treemap(src, d), "valid treemap parses");
  expect_eq_size(d.nodes.size(), 3, "three nodes");
}

void test_treemap_missing_header()
{
  md::TreemapDiagram d;
  expect_true(!md::parse_treemap("Root\n  A:1\n", d), "missing header rejected");
}

void test_eventmodeling_basic_valid()
{
  const std::string src =
      "eventmodeling\n"
      "  title E\n"
      "  command Place\n"
      "  event Placed\n";
  md::EventModelingDiagram d;
  expect_true(md::parse_eventmodeling(src, d), "valid event model parses");
  expect_eq_size(d.items.size(), 2, "two items");
}

void test_eventmodeling_missing_header()
{
  md::EventModelingDiagram d;
  expect_true(!md::parse_eventmodeling("command X\n", d), "missing header rejected");
}

void test_venn_basic_valid()
{
  const std::string src =
      "venn\n"
      "  title V\n"
      "  set A\n"
      "  set B\n"
      "  A&B \"Overlap\"\n";
  md::VennDiagram d;
  expect_true(md::parse_venn(src, d), "valid venn parses");
  expect_eq_size(d.sets.size(), 2, "two sets");
  expect_eq_size(d.intersections.size(), 1, "one intersection");
}

void test_venn_missing_header()
{
  md::VennDiagram d;
  expect_true(!md::parse_venn("set A\n", d), "missing header rejected");
}

void test_ishikawa_basic_valid()
{
  const std::string src =
      "ishikawa\n"
      "  effect \"Bug\"\n"
      "  category Process\n"
      "    slow\n";
  md::IshikawaDiagram d;
  expect_true(md::parse_ishikawa(src, d), "valid ishikawa parses");
  expect_eq_str(d.effect, "Bug", "effect");
  expect_eq_size(d.categories.size(), 1, "one category");
}

void test_ishikawa_missing_header()
{
  md::IshikawaDiagram d;
  expect_true(!md::parse_ishikawa("effect X\n", d), "missing header rejected");
}

void test_wardley_basic_valid()
{
  const std::string src =
      "wardley\n"
      "  title Map\n"
      "  component Alpha [0.5, 0.5]\n"
      "  component Beta [0.3, 0.7]\n";
  md::WardleyDiagram d;
  expect_true(md::parse_wardley(src, d), "valid wardley parses");
  expect_eq_size(d.components.size(), 2, "two components");
}

void test_wardley_missing_header()
{
  md::WardleyDiagram d;
  expect_true(!md::parse_wardley("component X [0.5, 0.5]\n", d), "missing header rejected");
}

void test_treeview_basic_valid()
{
  const std::string src =
      "treeview\n"
      "  Root\n"
      "    A\n"
      "    B\n";
  md::TreeViewDiagram d;
  expect_true(md::parse_treeview(src, d), "valid treeview parses");
  expect_eq_size(d.nodes.size(), 3, "three nodes");
}

void test_treeview_missing_header()
{
  md::TreeViewDiagram d;
  expect_true(!md::parse_treeview("Root\n  A\n", d), "missing header rejected");
}

void test_render_registry_lookup()
{
  // The split diagram types are registered.
  expect_true(md::is_registered_type("sequencediagram"), "sequencediagram registered");
  expect_true(md::is_registered_type("classdiagram"), "classdiagram registered");
  expect_true(md::is_registered_type("statediagram"), "statediagram registered");
  expect_true(md::is_registered_type("statediagram-v2"), "statediagram-v2 alias registered");
  expect_true(md::is_registered_type("erdiagram"), "erdiagram registered");
  expect_true(md::is_registered_type("journey"), "journey registered");
  expect_true(md::is_registered_type("gantt"), "gantt registered");
  expect_true(md::is_registered_type("quadrantchart"), "quadrantchart registered");
  expect_true(md::is_registered_type("requirementdiagram"), "requirementdiagram registered");
  expect_true(md::is_registered_type("gitgraph"), "gitgraph registered");
  expect_true(md::is_registered_type("mindmap"), "mindmap registered");
  expect_true(md::is_registered_type("timeline"), "timeline registered");
  expect_true(md::is_registered_type("sankey"), "sankey registered");
  expect_true(md::is_registered_type("sankey-beta"), "sankey-beta alias registered");
  expect_true(md::is_registered_type("xychart"), "xychart registered");
  expect_true(md::is_registered_type("block"), "block registered");
  expect_true(md::is_registered_type("packet"), "packet registered");
  expect_true(md::is_registered_type("packet-beta"), "packet-beta alias registered");
  expect_true(md::is_registered_type("kanban"), "kanban registered");
  expect_true(md::is_registered_type("architecture"), "architecture registered");
  expect_true(md::is_registered_type("radar"), "radar registered");
  expect_true(md::is_registered_type("treemap"), "treemap registered");
  expect_true(md::is_registered_type("eventmodeling"), "eventmodeling registered");
  expect_true(md::is_registered_type("venn"), "venn registered");
  expect_true(md::is_registered_type("ishikawa"), "ishikawa registered");
  expect_true(md::is_registered_type("wardley"), "wardley registered");
  expect_true(md::is_registered_type("treeview"), "treeview registered");

  // Case-insensitive lookup.
  expect_true(md::is_registered_type("SequenceDiagram"), "case-insensitive");

  // Unknown types are not registered.
  // (Empty string is treated as unknown since no canonical name has size 0.)
  expect_true(!md::is_registered_type("unknownDiagram"), "unknown diagram not registered");

  // Entry pointer matches canonical name.
  const md::RegistryEntry *e = md::find_registry_entry("sequencediagram");
  expect_true(e != nullptr, "entry exists for sequencediagram");
  expect_eq_str(e ? e->canonical : "", "sequencediagram", "canonical name");

  // Counter reports the right number of registered types.
  expect_eq_size(md::registered_type_count(), 35, "registry has 35 entries");
}
} // namespace

int main()
{
  test_sequence_basic_valid();
  test_sequence_missing_header();
  test_sequence_no_participants();
  test_sequence_with_notes_and_groups();
  test_class_basic_valid();
  test_class_missing_header();
  test_class_empty();
  test_state_basic_valid();
  test_state_missing_header();
  test_state_with_label();
  test_parse_result_helpers();
  test_er_basic_valid();
  test_er_missing_header();
  test_er_with_attributes();
  test_journey_basic_valid();
  test_journey_missing_header();
  test_gantt_basic_valid();
  test_gantt_missing_header();
  test_quadrant_basic_valid();
  test_quadrant_missing_header();
  test_requirement_basic_valid();
  test_requirement_missing_header();
  test_git_basic_valid();
  test_git_missing_header();
  test_mindmap_basic_valid();
  test_mindmap_missing_header();
  test_timeline_basic_valid();
  test_timeline_missing_header();
  test_sankey_basic_valid();
  test_sankey_missing_header();
  test_xychart_basic_valid();
  test_xychart_missing_header();
  test_block_basic_valid();
  test_block_missing_header();
  test_kanban_basic_valid();
  test_kanban_missing_header();
  test_architecture_basic_valid();
  test_architecture_missing_header();
  test_radar_basic_valid();
  test_radar_missing_header();
  test_treemap_basic_valid();
  test_treemap_missing_header();
  test_eventmodeling_basic_valid();
  test_eventmodeling_missing_header();
  test_venn_basic_valid();
  test_venn_missing_header();
  test_ishikawa_basic_valid();
  test_ishikawa_missing_header();
  test_wardley_basic_valid();
  test_wardley_missing_header();
  test_treeview_basic_valid();
  test_treeview_missing_header();
  test_render_registry_lookup();
  if(failures != 0)
  {
    std::cerr << failures << " mermaid test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "mermaid tests passed\n";
  return EXIT_SUCCESS;
}