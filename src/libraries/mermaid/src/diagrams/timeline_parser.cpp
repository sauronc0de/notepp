// ── timeline_parser.cpp ────────────────────────────────────────────────────
//
// Timeline diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <sstream>
#include <string>
#include <string_view>

#include "string_utils.hpp"

namespace MermaidDiagrams
{
namespace timelineparser
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
} // namespace timelineparser

bool parse_timeline(std::string_view src, TimelineDiagram &out)
{
  using namespace timelineparser;
  out = TimelineDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "timeline"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "title "))
    {
      out.title = std::string(tr(line.substr(6)));
      continue;
    }
    if(sw(ll, "section "))
    {
      out.periods.push_back({std::string(tr(line.substr(8))), {}});
      continue;
    }
    // period lines: "2002 : event1 : event2 : event3"
    std::size_t first_col = line.find(':');
    if(first_col != std::string_view::npos)
    {
      std::string period = std::string(tr(line.substr(0, first_col)));
      std::string rest2 = std::string(tr(line.substr(first_col + 1)));
      TLPeriod p;
      p.label = period;
      std::istringstream ss2(rest2);
      std::string tok;
      while(std::getline(ss2, tok, ':'))
      {
        std::string ev = std::string(StringUtils::trim(tok));
        if(!ev.empty()) p.events.push_back(ev);
      }
      out.periods.push_back(p);
    }
    else if(!line.empty())
    {
      out.periods.push_back({std::string(line), {}});
    }
  }
  return header;
}
} // namespace MermaidDiagrams
