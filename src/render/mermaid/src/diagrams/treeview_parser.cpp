// ── treeview_parser.cpp ────────────────────────────────────────────────────
//
// Treeview diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace treeviewparser
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
        if(c == ' ')
          indent++;
        else if(c == '\t')
          indent += 2;
        else
          break;
      }
      out = t;
      return true;
    }
    return false;
  }
};
} // namespace treeviewparser

bool parse_treeview(std::string_view src, TreeViewDiagram &out)
{
  using namespace treeviewparser;
  out = TreeViewDiagram{};
  bool header = false;
  IndentLineCursor L{src};
  std::string_view line;
  int indent = 0;
  std::vector<int> parent_at_level(20, -1);
  while(L.next(line, indent))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "treeview"))
      {
        header = true;
        continue;
      }
      continue;
    }
    int level = indent / 2;
    std::string_view l = tr(line);
    if(l.empty()) continue;
    while(!l.empty() && (static_cast<unsigned char>(l[0]) == 0xE2 || l[0] == '|' || l[0] == '-' || l[0] == '+' || l[0] == ' ' || l[0] == '`' || l[0] == '\\'))
    {
      if(l.size() >= 3 && static_cast<unsigned char>(l[0]) == 0xE2)
        l = l.substr(3);
      else
        l = l.substr(1);
    }
    l = tr(l);
    if(l.empty()) continue;
    std::string lbl = strip_quotes(l);
    int par = level > 0 ? parent_at_level[level - 1] : -1;
    TVNode node;
    node.label = lbl;
    node.parent = par;
    int ni = static_cast<int>(out.nodes.size());
    if(par >= 0) out.nodes[par].children.push_back(ni);
    parent_at_level[std::min(level, 19)] = ni;
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}
} // namespace MermaidDiagrams
