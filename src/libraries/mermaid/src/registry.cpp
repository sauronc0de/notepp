// ── registry.cpp ───────────────────────────────────────────────────────────
//
// Render registry: maps a diagram type name to its parser and renderer.
// The registry is data-driven so new diagram types can be registered by
// adding a single entry, without touching the dispatch chain in
// markdown_support.
//
// Each registered entry has a canonical type name (the preferred alias
// stored in user-visible strings) and a list of accepted aliases. A
// case-insensitive lookup is performed against the aliases.

#include "mermaid_diagrams.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace
{

// Equality check for std::string_view with ASCII case-folding. Mermaid type
// names are ASCII so a per-character tolower is sufficient.
bool ci_equal(std::string_view a, std::string_view b)
{
  if(a.size() != b.size()) return false;
  for(std::size_t i = 0; i < a.size(); ++i)
  {
    const char ca = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
    const char cb = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
    if(ca != cb) return false;
  }
  return true;
}

// Per-type parse / render wrappers. We store function pointers with explicit
// types using a small shim that hides the diagram type. The diagram value
// pointer is typed as `void*` for storage; the entry carries the right
// concrete type via the lambda's call operator.
template <typename ParseFn, typename RenderFn>
struct TypedEntry
{
  std::string_view  canonical;
  std::string_view  alias;
  ParseFn           parse;
  RenderFn          render;
};

template <typename ParseFn, typename RenderFn>
TypedEntry<ParseFn, RenderFn> make_entry(std::string_view canonical,
                                         std::string_view alias,
                                         ParseFn parse,
                                         RenderFn render)
{
  return TypedEntry<ParseFn, RenderFn>{canonical, alias, parse, render};
}

// Storage: the registry is a single std::array of variant entries. We avoid
// std::variant for simplicity and store the dispatch as erased function
// pointers in the public RegistryEntry struct.
struct StoredEntry
{
  std::string_view canonical;
  std::string_view alias;
  bool (*parse)(std::string_view, void *);
  void (*render)(const void *, int);
};

// Trampolines: each registered diagram type has a concrete parse / render
// function. We forward those to the existing parse_X / render_X functions.

#define MMD_TRAMPOLINES(PARSE_FN, RENDER_FN, TYPE)                             \
  static bool PARSE_FN##_trampoline(std::string_view body, void *out)          \
  {                                                                            \
    auto *typed = static_cast<TYPE *>(out);                                     \
    return PARSE_FN(body, *typed);                                              \
  }                                                                            \
  static void RENDER_FN##_trampoline(const void *value, int id)                \
  {                                                                            \
    const auto *typed = static_cast<const TYPE *>(value);                       \
    RENDER_FN(*typed, id);                                                      \
  }

// Only define the trampolines for the diagram types that are split out
// into their own translation units at the time the registry is built.
// Additional types can be added later by extending this list.
MMD_TRAMPOLINES(parse_sequence, render_sequence, SequenceDiagram)
MMD_TRAMPOLINES(parse_class, render_class, ClassDiagram)
MMD_TRAMPOLINES(parse_state, render_state, StateDiagram)
MMD_TRAMPOLINES(parse_er, render_er, ERDiagram)
MMD_TRAMPOLINES(parse_journey, render_journey, JourneyDiagram)
MMD_TRAMPOLINES(parse_gantt, render_gantt, GanttDiagram)
MMD_TRAMPOLINES(parse_quadrant, render_quadrant, QuadrantDiagram)
MMD_TRAMPOLINES(parse_requirement, render_requirement, RequirementDiagram)
MMD_TRAMPOLINES(parse_git, render_git, GitDiagram)
MMD_TRAMPOLINES(parse_mindmap, render_mindmap, MindmapDiagram)
MMD_TRAMPOLINES(parse_timeline, render_timeline, TimelineDiagram)
MMD_TRAMPOLINES(parse_sankey, render_sankey, SankeyDiagram)

constexpr std::array<StoredEntry, 14> kEntries = {{
    {"sequencediagram",     "sequencediagram",     &parse_sequence_trampoline,     &render_sequence_trampoline},
    {"classdiagram",        "classdiagram",        &parse_class_trampoline,        &render_class_trampoline},
    {"statediagram",        "statediagram",        &parse_state_trampoline,        &render_state_trampoline},
    {"statediagram",        "statediagram-v2",     &parse_state_trampoline,        &render_state_trampoline},
    {"erdiagram",           "erdiagram",           &parse_er_trampoline,           &render_er_trampoline},
    {"journey",             "journey",             &parse_journey_trampoline,      &render_journey_trampoline},
    {"gantt",               "gantt",               &parse_gantt_trampoline,        &render_gantt_trampoline},
    {"quadrantchart",       "quadrantchart",       &parse_quadrant_trampoline,     &render_quadrant_trampoline},
    {"requirementdiagram",  "requirementdiagram",  &parse_requirement_trampoline,  &render_requirement_trampoline},
    {"gitgraph",            "gitgraph",            &parse_git_trampoline,          &render_git_trampoline},
    {"mindmap",             "mindmap",             &parse_mindmap_trampoline,      &render_mindmap_trampoline},
    {"timeline",            "timeline",            &parse_timeline_trampoline,     &render_timeline_trampoline},
    {"sankey",              "sankey",              &parse_sankey_trampoline,       &render_sankey_trampoline},
    {"sankey",              "sankey-beta",         &parse_sankey_trampoline,       &render_sankey_trampoline},
}};
} // namespace

const RegistryEntry *find_registry_entry(std::string_view mermaid_type)
{
  for(const auto &e : kEntries)
  {
    if(ci_equal(e.alias, mermaid_type))
    {
      static thread_local RegistryEntry result;
      result.canonical = e.canonical.data();
      result.parse_any = e.parse;
      result.render_any = e.render;
      return &result;
    }
  }
  return nullptr;
}

bool is_registered_type(std::string_view mermaid_type)
{
  return find_registry_entry(mermaid_type) != nullptr;
}

std::size_t registered_type_count()
{
  return kEntries.size();
}

} // namespace MermaidDiagrams
