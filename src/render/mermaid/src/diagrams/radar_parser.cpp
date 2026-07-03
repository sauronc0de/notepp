// ── radar_parser.cpp ───────────────────────────────────────────────────────
//
// Radar chart diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace radarparser
{
using RadarLineCursor = LineCursor;
} // namespace radarparser

bool parse_radar(std::string_view src, RadarDiagram &out)
{
  using namespace radarparser;
  out = RadarDiagram{};
  RadarLineCursor L{src};
  std::string_view line;
  bool header = false;
  RadarCurve cur_curve;
  bool in_curve = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "radar-beta") || sw(ll, "radar"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "title "))
    {
      out.title = strip_quotes(line.substr(6));
      continue;
    }
    if(sw(ll, "max ") || sw(ll, "accmax "))
    {
      out.max_val = std::strtof(std::string(tr(line.substr(ll.find(' ') + 1))).c_str(), nullptr);
      continue;
    }
    if(sw(ll, "axis "))
    {
      std::string_view r = tr(line.substr(5));
      if(!r.empty() && r.front() == '[')
      {
        std::size_t e2 = r.find(']');
        if(e2 != std::string_view::npos) r = r.substr(1, e2 - 1);
      }
      std::string r_s(r);
      std::istringstream ss2(r_s);
      std::string tok;
      while(std::getline(ss2, tok, ',')) out.axes.push_back(strip_quotes(tr(tok)));
      continue;
    }
    if(line == "}")
    {
      if(in_curve) out.curves.push_back(cur_curve);
      in_curve = false;
      cur_curve = RadarCurve{};
      continue;
    }
    if(!line.empty() && line.back() == '{')
    {
      in_curve = true;
      cur_curve.name = std::string(tr(line.substr(0, line.size() - 1)));
      continue;
    }
    if(in_curve && sw(ll, "data ["))
    {
      std::size_t b = line.find('[');
      std::size_t e2 = line.find(']');
      if(b != std::string_view::npos && e2 != std::string_view::npos)
      {
        std::string inner = std::string(line.substr(b + 1, e2 - b - 1));
        std::istringstream ss2(inner);
        std::string tok;
        while(std::getline(ss2, tok, ',')) cur_curve.values.push_back(std::strtof(std::string(tr(tok)).c_str(), nullptr));
      }
      continue;
    }
    std::size_t col = line.find(':');
    if(col != std::string_view::npos && !in_curve)
    {
      std::string axis_name = strip_quotes(line.substr(0, col));
      float val = std::strtof(std::string(tr(line.substr(col + 1))).c_str(), nullptr);
      out.axes.push_back(axis_name);
      if(out.curves.empty()) out.curves.push_back({"Values", {}});
      out.curves[0].values.push_back(val);
      out.max_val = std::max(out.max_val, val);
    }
  }
  if(in_curve) out.curves.push_back(cur_curve);
  return header && (!out.axes.empty() || !out.curves.empty());
}
} // namespace MermaidDiagrams
