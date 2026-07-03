// ── quadrant_parser.cpp ────────────────────────────────────────────────────
//
// Quadrant chart diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace quadrantparser
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

static std::string strip_quotes(std::string_view s)
{
  s = tr(s);
  if(s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
    return std::string(s.substr(1, s.size() - 2));
  return std::string(s);
}
} // namespace quadrantparser

bool parse_quadrant(std::string_view src, QuadrantDiagram &out)
{
  using namespace quadrantparser;
  out = QuadrantDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "quadrantchart"))
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
    if(sw(ll, "x-axis "))
    {
      std::string_view r = tr(line.substr(8));
      std::size_t ar = r.find("-->");
      if(ar != std::string_view::npos)
      {
        out.x_low = strip_quotes(r.substr(0, ar));
        out.x_high = strip_quotes(tr(r.substr(ar + 3)));
      }
      else
        out.x_low = std::string(r);
      continue;
    }
    if(sw(ll, "y-axis "))
    {
      std::string_view r = tr(line.substr(8));
      std::size_t ar = r.find("-->");
      if(ar != std::string_view::npos)
      {
        out.y_low = strip_quotes(r.substr(0, ar));
        out.y_high = strip_quotes(tr(r.substr(ar + 3)));
      }
      else
        out.y_low = std::string(r);
      continue;
    }
    if(sw(ll, "quadrant-1 "))
    {
      out.q1 = std::string(tr(line.substr(11)));
      continue;
    }
    if(sw(ll, "quadrant-2 "))
    {
      out.q2 = std::string(tr(line.substr(11)));
      continue;
    }
    if(sw(ll, "quadrant-3 "))
    {
      out.q3 = std::string(tr(line.substr(11)));
      continue;
    }
    if(sw(ll, "quadrant-4 "))
    {
      out.q4 = std::string(tr(line.substr(11)));
      continue;
    }
    // point: Name: [x, y]
    std::size_t col = line.find(':');
    if(col == std::string_view::npos) continue;
    std::string name = strip_quotes(line.substr(0, col));
    std::string_view coords = tr(line.substr(col + 1));
    if(!coords.empty() && coords[0] == '[')
    {
      std::size_t ce = coords.find(']');
      if(ce != std::string_view::npos)
      {
        std::string cs = std::string(coords.substr(1, ce - 1));
        std::size_t comma = cs.find(',');
        if(comma != std::string::npos)
        {
          float x = std::strtof(cs.substr(0, comma).c_str(), nullptr);
          float y = std::strtof(cs.substr(comma + 1).c_str(), nullptr);
          out.points.push_back({name, x, y});
        }
      }
    }
  }
  return header;
}
} // namespace MermaidDiagrams
