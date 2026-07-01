// ── ishikawa_parser.cpp ────────────────────────────────────────────────────
//
// Ishikawa (fishbone) diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstring>
#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace ishikawaparser
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
} // namespace ishikawaparser

bool parse_ishikawa(std::string_view src, IshikawaDiagram &out)
{
  using namespace ishikawaparser;
  out = IshikawaDiagram{};
  IndentLineCursor L{src};
  std::string_view line;
  bool header = false;
  int indent = 0;
  while(L.next(line, indent))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "ishikawa"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "effect "))
    {
      out.effect = strip_quotes(line.substr(7));
      continue;
    }
    if(sw(ll, "category "))
    {
      out.categories.push_back({strip_quotes(line.substr(9)), {}});
      continue;
    }
    std::string_view l = tr(line);
    if(!l.empty() && !sw(lc(l), "cause ") && !sw(lc(l), "sub ") && !out.categories.empty())
    {
      IshikawaCause c;
      c.text = std::string(l);
      out.categories.back().causes.push_back(c);
    }
    if(sw(lc(l), "cause ") || sw(lc(l), "sub "))
    {
      std::size_t sp = l.find(' ');
      if(!out.categories.empty())
      {
        IshikawaCause c;
        c.text = strip_quotes(l.substr(sp));
        out.categories.back().causes.push_back(c);
      }
    }
  }
  return header && !out.effect.empty();
}
} // namespace MermaidDiagrams
