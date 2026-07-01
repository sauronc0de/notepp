// ── wardley_parser.cpp ────────────────────────────────────────────────────
//
// Wardley map diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace wardleyparser
{

inline bool wardley_split_arrow(std::string_view line, std::string_view &lhs, std::string_view &rhs, std::string &lbl)
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

} // namespace wardleyparser

bool parse_wardley(std::string_view src, WardleyDiagram &out)
{
  using namespace wardleyparser;
  out = WardleyDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "wardley"))
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
    if(sw(ll, "component ") || sw(ll, "note "))
    {
      std::size_t kw_len = sw(ll, "component ") ? 10 : 5;
      std::string_view rest = tr(line.substr(kw_len));
      std::size_t b1 = rest.find('[');
      std::size_t b2 = rest.find(']');
      std::string name = std::string(b1 != std::string_view::npos ? tr(rest.substr(0, b1)) : rest);
      float vis = 0.5f, evo = 0.5f;
      if(b1 != std::string_view::npos && b2 != std::string_view::npos)
      {
        std::string coords = std::string(rest.substr(b1 + 1, b2 - b1 - 1));
        std::size_t comma = coords.find(',');
        if(comma != std::string::npos)
        {
          vis = std::strtof(coords.substr(0, comma).c_str(), nullptr);
          evo = std::strtof(coords.substr(comma + 1).c_str(), nullptr);
        }
      }
      out.components.push_back({name, vis, evo});
      continue;
    }
    std::string_view lhs, rhs;
    std::string lbl;
    if(wardley_split_arrow(line, lhs, rhs, lbl)) out.links.push_back({std::string(lhs), std::string(rhs)});
  }
  return header && !out.components.empty();
}
} // namespace MermaidDiagrams
