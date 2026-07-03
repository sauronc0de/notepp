// ── journey_parser.cpp ─────────────────────────────────────────────────────
//
// User journey diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace journeyparser
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
} // namespace journeyparser

bool parse_journey(std::string_view src, JourneyDiagram &out)
{
  using namespace journeyparser;
  out = JourneyDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "journey"))
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
      out.sections.push_back({std::string(tr(line.substr(8))), {}});
      continue;
    }
    // task line: "Task name: score: actor1, actor2"
    std::size_t c1 = line.find(':');
    if(c1 == std::string_view::npos) continue;
    std::string name = std::string(tr(line.substr(0, c1)));
    std::string_view rest2 = tr(line.substr(c1 + 1));
    std::size_t c2 = rest2.find(':');
    int score = 3;
    std::vector<std::string> actors;
    if(c2 != std::string_view::npos)
    {
      score = std::atoi(std::string(tr(rest2.substr(0, c2))).c_str());
      std::string_view ac = tr(rest2.substr(c2 + 1));
      std::string acs(ac);
      std::istringstream ss(acs);
      std::string tok;
      while(std::getline(ss, tok, ',')) actors.push_back(std::string(tr(tok)));
    }
    else
    {
      score = std::atoi(std::string(rest2).c_str());
    }
    if(out.sections.empty()) out.sections.push_back({"", {}});
    out.sections.back().tasks.push_back({name, score, actors});
  }
  return header;
}
} // namespace MermaidDiagrams
