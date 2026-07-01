// ── treemap_parser.cpp ─────────────────────────────────────────────────────
//
// Treemap diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace treemapparser
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
} // namespace treemapparser

bool parse_treemap(std::string_view src, TreemapDiagram &out)
{
  using namespace treemapparser;
  out = TreemapDiagram{};
  IndentLineCursor L{src};
  std::string_view line;
  bool header = false;
  std::vector<int> parent_at_level(20, -1);
  while(L.next(line, [&](std::string_view &o, int &i) -> bool {
    std::string_view raw = line;
    int indent2 = 0;
    for(char c : raw)
    {
      if(c == ' ') indent2++;
      else if(c == '\t') indent2 += 2;
      else break;
    }
    (void)indent2;
    o = line;
    i = indent2;
    return true;
  }))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "treemap-beta") || sw(ll, "treemap"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "title ")) continue;
    int indent = 0;
    for(char c : line)
    {
      if(c == ' ') indent++;
      else if(c == '\t') indent += 2;
      else break;
    }
    int level = indent / 2;
    std::string_view l = tr(line);
    if(l.empty()) continue;
    std::size_t col = l.find(':');
    std::string name = (col != std::string_view::npos) ? strip_quotes(l.substr(0, col)) : std::string(l);
    float val = (col != std::string_view::npos) ? std::strtof(std::string(tr(l.substr(col + 1))).c_str(), nullptr) : 0.0f;
    int par = level > 0 ? parent_at_level[level - 1] : -1;
    TreemapNode node;
    node.name = name;
    node.value = val;
    node.parent = par;
    int ni = static_cast<int>(out.nodes.size());
    if(par >= 0) out.nodes[par].children.push_back(ni);
    parent_at_level[level] = ni;
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}
} // namespace MermaidDiagrams
