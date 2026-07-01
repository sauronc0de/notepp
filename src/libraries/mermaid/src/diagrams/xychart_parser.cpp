// ── xychart_parser.cpp ─────────────────────────────────────────────────────
//
// XY chart diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace xychartparser
{

static void apply_xy_axis_config(XYAxisConfig &cfg, std::string_view key, std::string_view value)
{
  if(key == "showLabel") cfg.show_label = parse_bool_value(value, cfg.show_label);
  else if(key == "labelPadding") cfg.label_padding = parse_float_value(value, cfg.label_padding);
  else if(key == "showTitle") cfg.show_title = parse_bool_value(value, cfg.show_title);
  else if(key == "titlePadding") cfg.title_padding = parse_float_value(value, cfg.title_padding);
  else if(key == "showTick") cfg.show_tick = parse_bool_value(value, cfg.show_tick);
  else if(key == "tickLength") cfg.tick_length = parse_float_value(value, cfg.tick_length);
  else if(key == "tickWidth") cfg.tick_width = parse_float_value(value, cfg.tick_width);
  else if(key == "showAxisLine") cfg.show_axis_line = parse_bool_value(value, cfg.show_axis_line);
  else if(key == "axisLineWidth") cfg.axis_line_width = parse_float_value(value, cfg.axis_line_width);
}

static void parse_xychart_frontmatter(std::string_view src, XYDiagram &out)
{
  std::size_t p = 0;
  bool in_xy = false;
  int xy_indent = -1;
  XYAxisConfig *axis = nullptr;
  int axis_indent = -1;

  while(p < src.size())
  {
    std::size_t e = src.find('\n', p);
    if(e == std::string_view::npos) e = src.size();
    std::string_view raw = src.substr(p, e - p);
    if(!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
    p = (e < src.size()) ? e + 1 : e;
    const std::string_view line = tr(raw);
    if(line.empty()) continue;
    if(line != "---") return;
    break;
  }

  while(p < src.size())
  {
    std::size_t e = src.find('\n', p);
    if(e == std::string_view::npos) e = src.size();
    std::string_view raw = src.substr(p, e - p);
    if(!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
    p = (e < src.size()) ? e + 1 : e;

    const std::string_view line = tr(raw);
    if(line == "---") break;
    if(line.empty() || sw(line, "#")) continue;

    const int indent = count_leading_spaces(raw);
    std::string_view key, value;
    if(!split_yaml_pair(line, key, value)) continue;

    if((key == "xyChart" || key == "xychart") && value.empty())
    {
      in_xy = true;
      xy_indent = indent;
      axis = nullptr;
      continue;
    }
    if(!in_xy) continue;
    if(indent <= xy_indent)
    {
      in_xy = false;
      axis = nullptr;
      continue;
    }
    if(axis && indent <= axis_indent) axis = nullptr;

    if((key == "xAxis" || key == "xaxis") && value.empty())
    {
      axis = &out.config.x_axis;
      axis_indent = indent;
      continue;
    }
    if((key == "yAxis" || key == "yaxis") && value.empty())
    {
      axis = &out.config.y_axis;
      axis_indent = indent;
      continue;
    }
    if(axis)
    {
      apply_xy_axis_config(*axis, key, value);
      continue;
    }

    if(key == "width") out.config.width = std::max(120.0f, parse_float_value(value, out.config.width));
    else if(key == "height") out.config.height = std::max(120.0f, parse_float_value(value, out.config.height));
    else if(key == "showTitle") out.config.show_title = parse_bool_value(value, out.config.show_title);
    else if(key == "showDataLabel") out.config.show_data_label = parse_bool_value(value, out.config.show_data_label);
    else if(key == "showDataLabelOutsideBar") out.config.show_data_label_outside_bar = parse_bool_value(value, out.config.show_data_label_outside_bar);
    else if(key == "titlePadding") out.config.title_padding = parse_float_value(value, out.config.title_padding);
    else if(key == "plotReservedSpacePercent") out.config.plot_reserved_space_percent = parse_float_value(value, out.config.plot_reserved_space_percent);
    else if(key == "chartOrientation")
    {
      const std::string v = lc(strip_quotes(value));
      if(v == "horizontal") out.horizontal = true;
    }
  }
}

} // namespace xychartparser

bool parse_xychart(std::string_view src, XYDiagram &out)
{
  using namespace xychartparser;
  out = XYDiagram{};
  parse_xychart_frontmatter(src, out);
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "xychart-beta") || sw(ll, "xychart"))
      {
        header = true;
        if(ll.find("horizontal") != std::string::npos) out.horizontal = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "title "))
    {
      out.title = strip_quotes(line.substr(6));
      continue;
    }
    if(sw(ll, "x-axis "))
    {
      std::string_view r = tr(line.substr(7));
      out.x_title = parse_leading_label(r);
      std::string inner;
      if(read_bracket_list(L, r, inner))
      {
        for(const std::string &tok : split_csv_items(inner)) out.x_labels.push_back(tok);
      }
      else
      {
        std::size_t ar = r.find("-->");
        if(ar != std::string_view::npos)
        {
          out.x_labels.push_back(strip_quotes(strip_leading_quoted_label(r.substr(0, ar))));
          out.x_labels.push_back(strip_quotes(strip_leading_quoted_label(r.substr(ar + 3))));
        }
      }
      continue;
    }
    if(sw(ll, "y-axis "))
    {
      std::string_view r = tr(line.substr(7));
      out.y_title = parse_leading_label(r);
      std::size_t ar = r.find("-->");
      if(ar != std::string_view::npos)
      {
        out.y_min = std::strtof(std::string(strip_leading_quoted_label(r.substr(0, ar))).c_str(), nullptr);
        out.y_max = std::strtof(std::string(strip_leading_quoted_label(r.substr(ar + 3))).c_str(), nullptr);
        out.y_explicit = true;
      }
      continue;
    }
    bool is_bar = sw(ll, "bar ");
    bool is_line = sw(ll, "line ");
    if(is_bar || is_line)
    {
      std::string_view r = tr(line.substr(is_bar ? 4 : 5));
      std::string inner;
      if(read_bracket_list(L, r, inner))
      {
        XYSeries s;
        s.is_bar = is_bar;
        for(const std::string &tok : split_csv_items(inner))
        {
          float v = std::strtof(tok.c_str(), nullptr);
          s.data.push_back(v);
        }
        if(!s.data.empty())
        {
          if(!out.y_explicit)
          {
            for(float v : s.data)
            {
              out.y_min = std::min(out.y_min, v);
              out.y_max = std::max(out.y_max, v);
            }
          }
          out.series.push_back(s);
        }
      }
    }
  }
  return header && (!out.series.empty() || !out.x_labels.empty());
}
} // namespace MermaidDiagrams
