// ── block_parser.cpp ───────────────────────────────────────────────────────
//
// Block diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace blockparser
{

inline bool block_split_arrow(std::string_view line, std::string_view &lhs, std::string_view &rhs, std::string &lbl)
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
  std::string_view rest = tr(line.substr(best + best_len));
  // optional label "|...|"
  if(!rest.empty() && rest.front() == '|')
  {
    std::size_t ce = rest.find('|', 1);
    if(ce != std::string_view::npos)
    {
      lbl = strip_quotes(tr(rest.substr(1, ce - 1)));
      rest = tr(rest.substr(ce + 1));
    }
  }
  rhs = rest;
  return !lhs.empty() && !rhs.empty();
}

} // namespace blockparser

bool parse_block(std::string_view src, BlockDiagram &out)
{
  using namespace blockparser;
  out = BlockDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  std::unordered_map<std::string, int> idx;
  auto ensure_node = [&](const std::string &id, const std::string &lbl, const std::string &shape) {
    auto it = idx.find(id);
    if(it != idx.end()) return it->second;
    int n = static_cast<int>(out.nodes.size());
    out.nodes.push_back({id, lbl.empty() ? id : lbl, shape});
    idx[id] = n;
    return n;
  };
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "block-beta") || sw(ll, "block"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "columns "))
    {
      out.columns = std::atoi(std::string(tr(line.substr(8))).c_str());
      continue;
    }
    std::string_view lhs, rhs;
    std::string lbl2;
    if(block_split_arrow(line, lhs, rhs, lbl2))
    {
      std::string f(lhs);
      std::string t(rhs);
      ensure_node(f, "", "");
      ensure_node(t, "", "");
      out.edges.push_back({f, t, lbl2});
      continue;
    }
    std::string_view l = line;
    std::size_t b1 = l.find('[');
    std::size_t b2 = l.find(']');
    std::size_t p1 = l.find('(');
    std::size_t p2 = l.find(')');
    if(b1 != std::string_view::npos && b2 != std::string_view::npos)
    {
      std::string id2 = std::string(tr(l.substr(0, b1)));
      std::string lbl2b = strip_quotes(l.substr(b1 + 1, b2 - b1 - 1));
      ensure_node(id2, lbl2b, "rect");
    }
    else if(p1 != std::string_view::npos && p2 != std::string_view::npos)
    {
      std::string id2 = std::string(tr(l.substr(0, p1)));
      std::string lbl2b = strip_quotes(l.substr(p1 + 1, p2 - p1 - 1));
      ensure_node(id2, lbl2b, "round");
    }
    else if(!line.empty() && line.find(' ') == std::string_view::npos)
    {
      ensure_node(std::string(line), "", "");
    }
  }
  return header && !out.nodes.empty();
}
} // namespace MermaidDiagrams
