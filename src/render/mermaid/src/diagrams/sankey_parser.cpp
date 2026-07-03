// ── sankey_parser.cpp ───────────────────────────────────────────────────────
//
// Sankey diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace sankeyparser
{
struct LineCursor
{
  std::string_view src;
  std::size_t pos = 0;
  explicit LineCursor(std::string_view s) : src(s) {}
  bool next(std::string_view &out)
  {
    while(pos < src.size())
    {
      std::size_t e = src.find('\n', pos);
      if(e == std::string_view::npos) e = src.size();
      std::string_view line = src.substr(pos, e - pos);
      pos = (e < src.size()) ? e + 1 : e;
      std::size_t a = 0, b = line.size();
      while(a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) ++a;
      while(b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) --b;
      std::string_view trimmed = line.substr(a, b - a);
      if(trimmed.empty()) continue;
      out = trimmed;
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
} // namespace sankeyparser

bool parse_sankey(std::string_view src, SankeyDiagram &out)
{
  using namespace sankeyparser;
  out = SankeyDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "sankey-beta") || sw(ll, "sankey"))
      {
        header = true;
        continue;
      }
      continue;
    }
    // CSV: source,target,value
    std::string ls(line);
    std::size_t c1 = ls.find(',');
    if(c1 == std::string::npos) continue;
    std::size_t c2 = ls.find(',', c1 + 1);
    if(c2 == std::string::npos) continue;
    std::string src2 = std::string(tr(ls.substr(0, c1)));
    std::string tgt = std::string(tr(ls.substr(c1 + 1, c2 - c1 - 1)));
    float val = std::strtof(ls.substr(c2 + 1).c_str(), nullptr);
    if(src2.empty() || tgt.empty()) continue;
    out.flows.push_back({src2, tgt, val});
  }
  return header && !out.flows.empty();
}
} // namespace MermaidDiagrams
