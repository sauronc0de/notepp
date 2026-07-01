// ── kanban_parser.cpp ──────────────────────────────────────────────────────
//
// Kanban diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace kanbanparser
{
struct IndentLineCursor
{
  std::string_view src;
  std::size_t pos = 0;
  bool next(std::string_view &out, int &indent)
  {
    while(pos < src.size())
    {
      std::size_t e = src.find('\n', pos);
      if(e == std::string_view::npos) e = src.size();
      std::string_view raw = src.substr(pos, e - pos);
      pos = (e < src.size()) ? e + 1 : e;
      std::size_t a = 0, b = raw.size();
      while(a < b && (raw[a] == ' ' || raw[a] == '\t' || raw[a] == '\r')) ++a;
      while(b > a && (raw[b - 1] == ' ' || raw[b - 1] == '\t' || raw[b - 1] == '\r')) --b;
      std::string_view t = raw.substr(a, b - a);
      if(t.empty()) continue;
      indent = 0;
      for(char c : raw)
      {
        if(c == ' ') indent++;
        else if(c == '\t') indent += 2;
        else break;
      }
      out = t;
      return true;
    }
    return false;
  }
};
} // namespace kanbanparser

bool parse_kanban(std::string_view src, KanbanDiagram &out)
{
  using namespace kanbanparser;
  out = KanbanDiagram{};
  IndentLineCursor L{src};
  std::string_view line;
  bool header = false;
  int indent = 0;
  int col_indent = -1;
  while(L.next(line, indent))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "kanban"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(line == "{" || line == "}") continue;
    if(!line.empty() && line.front() == '@') continue;
    std::string_view item_part = line;
    std::string desc;
    std::size_t rb = line.rfind(']');
    if(rb != std::string_view::npos)
    {
      std::size_t col = line.find(':', rb + 1);
      if(col != std::string_view::npos)
      {
        desc = std::string(tr(line.substr(col + 1)));
        item_part = line.substr(0, rb + 1);
      }
    }
    std::size_t b1 = item_part.find('[');
    std::size_t b2 = item_part.find(']');
    std::string id2, lbl;
    if(b1 != std::string_view::npos && b2 != std::string_view::npos)
    {
      id2 = std::string(tr(item_part.substr(0, b1)));
      lbl = std::string(tr(item_part.substr(b1 + 1, b2 - b1 - 1)));
    }
    else
    {
      id2 = std::string(item_part);
      lbl = id2;
    }
    if(id2.empty()) continue;
    if(col_indent < 0) col_indent = indent;
    if(indent <= col_indent)
    {
      out.columns.push_back({id2, lbl, {}});
    }
    else
    {
      if(!out.columns.empty()) out.columns.back().cards.push_back({id2, lbl, desc});
    }
  }
  return header && !out.columns.empty();
}
} // namespace MermaidDiagrams
