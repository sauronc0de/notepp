// ── mindmap_parser.cpp ─────────────────────────────────────────────────────
//
// Mindmap diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace mindmapparser
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

static bool sw(std::string_view s, std::string_view p)
{
  if(s.size() < p.size()) return false;
  for(std::size_t i = 0; i < p.size(); ++i)
    if(s[i] != p[i]) return false;
  return true;
}

static std::string_view tr(std::string_view s)
{
  std::size_t a = 0, b = s.size();
  while(a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while(b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

static std::string lc(std::string_view s)
{
  std::string out(s);
  for(char &c : out)
    if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}
} // namespace mindmapparser

bool parse_mindmap(std::string_view src, MindmapDiagram &out)
{
  using namespace mindmapparser;
  out = MindmapDiagram{};
  IndentLineCursor L{src};
  std::string_view line;
  bool header = false;
  int indent = 0;
  while(L.next(line, indent))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "mindmap"))
      {
        header = true;
        continue;
      }
      continue;
    }
    int level = indent / 2;
    std::string_view lbl = tr(line);
    if(lbl.size() >= 4 && sw(lbl, "((") && lbl.back() == ')' && lbl[lbl.size() - 2] == ')')
      lbl = lbl.substr(2, lbl.size() - 4);
    else if(lbl.size() >= 2 && lbl.front() == '(' && lbl.back() == ')')
      lbl = lbl.substr(1, lbl.size() - 2);
    else if(lbl.size() >= 2 && lbl.front() == '[' && lbl.back() == ']')
      lbl = lbl.substr(1, lbl.size() - 2);
    else if(lbl.size() >= 4 && sw(lbl, "{{") && lbl.back() == '}' && lbl[lbl.size() - 2] == '}')
      lbl = lbl.substr(2, lbl.size() - 4);
    std::size_t icon = lbl.find("::icon(");
    if(icon != std::string_view::npos) lbl = tr(lbl.substr(0, icon));
    std::size_t cls = lbl.find(":::");
    if(cls != std::string_view::npos) lbl = tr(lbl.substr(0, cls));
    MindNode node;
    node.label = std::string(lbl);
    node.level = level;
    int ni = static_cast<int>(out.nodes.size());
    node.parent = -1;
    for(int i = ni - 1; i >= 0; --i)
    {
      if(out.nodes[i].level == level - 1)
      {
        node.parent = i;
        out.nodes[i].children.push_back(ni);
        break;
      }
    }
    out.nodes.push_back(node);
  }
  return header && !out.nodes.empty();
}
} // namespace MermaidDiagrams
