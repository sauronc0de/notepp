// ── gantt_parser.cpp ───────────────────────────────────────────────────────
//
// Gantt diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace MermaidDiagrams
{
namespace ganttparser
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
} // namespace ganttparser

bool parse_gantt(std::string_view src, GanttDiagram &out)
{
  using namespace ganttparser;
  out = GanttDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  int day_counter = 0;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "gantt"))
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
    if(sw(ll, "dateformat ") || sw(ll, "axisformat ") || sw(ll, "todaymarker ") ||
       sw(ll, "tickinterval ") || sw(ll, "weekday ") || sw(ll, "excludes "))
      continue;
    if(sw(ll, "section "))
    {
      out.sections.push_back({std::string(tr(line.substr(8))), {}});
      continue;
    }
    // task: name :flags, start, end   or   name :flags, after id, dur
    std::size_t col = line.find(':');
    if(col == std::string_view::npos) continue;
    std::string tname = std::string(tr(line.substr(0, col)));
    std::string_view spec = tr(line.substr(col + 1));
    GanttTask task;
    task.name = tname;
    std::string spec_s(spec);
    std::istringstream ss2(spec_s);
    std::string tok;
    std::vector<std::string> parts2;
    while(std::getline(ss2, tok, ',')) parts2.push_back(std::string(tr(tok)));
    int field = 0;
    for(auto &p : parts2)
    {
      std::string pl = lc(p);
      if(pl == "crit")
      {
        task.is_crit = true;
        continue;
      }
      if(pl == "milestone")
      {
        task.is_milestone = true;
        continue;
      }
      if(pl == "done" || pl == "active") continue;
      if(field == 0)
      {
        if(sw(pl, "after "))
        {
          task.after = std::string(tr(pl.substr(6)));
          field++;
        }
        else
        {
          task.id = p;
          field++;
        }
        continue;
      }
      int val = std::atoi(p.c_str());
      if(val > 0)
      {
        task.dur = val;
      }
      else
      {
        task.start_day = day_counter;
      }
      field++;
    }
    if(task.start_day == 0 && task.after.empty()) task.start_day = day_counter;
    day_counter += task.dur;
    if(out.sections.empty()) out.sections.push_back({"", {}});
    out.sections.back().tasks.push_back(task);
  }
  return header;
}
} // namespace MermaidDiagrams
