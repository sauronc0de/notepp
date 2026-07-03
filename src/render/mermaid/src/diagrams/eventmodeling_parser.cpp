// ── eventmodeling_parser.cpp ──────────────────────────────────────────────
//
// Event modeling diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace eventmodelingparser
{

inline bool em_split_arrow(std::string_view line, std::string_view &lhs, std::string_view &rhs, std::string &lbl)
{
  static const char *kOps[] = {"<-->", "-->", "<--", "-.->", "==>", "---"};
  std::size_t best = std::string_view::npos;
  std::size_t best_len = 0;
  for(const char *op : kOps)
  {
    std::size_t p = line.find(op);
    if(p != std::string_view::npos && (best == std::string_view::npos || p < best))
    {
      best = p;
      best_len = std::char_traits<char>::length(op);
    }
  }
  if(best == std::string_view::npos) return false;
  lhs = tr(line.substr(0, best));
  rhs = tr(line.substr(best + best_len));
  (void)lbl;
  return !lhs.empty() && !rhs.empty();
}

} // namespace eventmodelingparser

bool parse_eventmodeling(std::string_view src, EventModelingDiagram &out)
{
  using namespace eventmodelingparser;
  out = EventModelingDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "eventmodeling"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "title "))
    {
      out.title = strip_quotes(line.substr(6));
      continue;
    }
    if(sw(ll, "command "))
    {
      out.items.push_back({EMItem::T::Command, strip_quotes(line.substr(8))});
      continue;
    }
    if(sw(ll, "event "))
    {
      out.items.push_back({EMItem::T::Event, strip_quotes(line.substr(6))});
      continue;
    }
    if(sw(ll, "readmodel ") || sw(ll, "read_model "))
    {
      out.items.push_back({EMItem::T::ReadModel, strip_quotes(line.substr(ll.find(' ') + 1))});
      continue;
    }
    if(sw(ll, "policy "))
    {
      out.items.push_back({EMItem::T::Policy, strip_quotes(line.substr(7))});
      continue;
    }
    if(sw(ll, "processor "))
    {
      out.items.push_back({EMItem::T::Processor, strip_quotes(line.substr(10))});
      continue;
    }
    std::string_view lhs, rhs;
    std::string lbl;
    if(em_split_arrow(line, lhs, rhs, lbl)) out.links.push_back({std::string(lhs), std::string(rhs)});
  }
  return header && !out.items.empty();
}
} // namespace MermaidDiagrams
