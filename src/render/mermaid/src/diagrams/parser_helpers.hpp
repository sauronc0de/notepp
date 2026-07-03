// ── parser_helpers.hpp ─────────────────────────────────────────────────────
//
// Shared parsing helpers for the per-diagram parser files. Each function
// here was previously a static helper in mermaid_diagrams.cpp. Moving them
// into this header keeps every parser self-contained and lets each
// translation unit own a copy at namespace scope, avoiding Unity-build
// anonymous-namespace collisions between parsers.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "string_utils.hpp"

namespace MermaidDiagrams
{

inline bool sw(std::string_view s, std::string_view p)
{
  if(s.size() < p.size()) return false;
  for(std::size_t i = 0; i < p.size(); ++i)
    if(s[i] != p[i]) return false;
  return true;
}

inline std::string_view tr(std::string_view s)
{
  std::size_t a = 0, b = s.size();
  while(a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while(b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

inline std::string lc(std::string_view s)
{
  std::string out(s);
  for(char &c : out)
    if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

inline std::string strip_quotes(std::string_view s)
{
  s = tr(s);
  if(s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
    return std::string(s.substr(1, s.size() - 2));
  return std::string(s);
}

inline std::size_t find_unquoted(std::string_view s, char ch, std::size_t start = 0)
{
  char quote = 0;
  for(std::size_t i = start; i < s.size(); ++i)
  {
    const char c = s[i];
    if(quote)
    {
      if(c == quote) quote = 0;
    }
    else if(c == '"' || c == '\'')
    {
      quote = c;
    }
    else if(c == ch)
    {
      return i;
    }
  }
  return std::string_view::npos;
}

inline std::vector<std::string> split_csv_items(std::string_view s)
{
  std::vector<std::string> items;
  std::size_t start = 0;
  char quote = 0;
  for(std::size_t i = 0; i <= s.size(); ++i)
  {
    const char c = i < s.size() ? s[i] : ',';
    if(i < s.size())
    {
      if(quote)
      {
        if(c == quote) quote = 0;
      }
      else if(c == '"' || c == '\'')
      {
        quote = c;
      }
      else if(c != ',')
      {
        continue;
      }
    }
    std::string item = strip_quotes(tr(s.substr(start, i - start)));
    if(!item.empty()) items.push_back(std::move(item));
    start = i + 1;
  }
  return items;
}

inline std::string_view strip_leading_quoted_label(std::string_view s)
{
  s = tr(s);
  if(s.size() < 2 || (s.front() != '"' && s.front() != '\'')) return s;
  const char quote = s.front();
  const std::size_t close = s.find(quote, 1);
  if(close == std::string_view::npos) return s;
  return tr(s.substr(close + 1));
}

inline bool parse_bool_value(std::string_view s, bool defv)
{
  const std::string v = lc(tr(s));
  if(v == "true") return true;
  if(v == "false") return false;
  return defv;
}

inline float parse_float_value(std::string_view s, float defv)
{
  std::string v(strip_quotes(tr(s)));
  char *end = nullptr;
  const float parsed = std::strtof(v.c_str(), &end);
  return end != v.c_str() ? parsed : defv;
}

inline int count_leading_spaces(std::string_view s)
{
  int n = 0;
  while(n < static_cast<int>(s.size()) && s[static_cast<std::size_t>(n)] == ' ') ++n;
  return n;
}

inline bool split_yaml_pair(std::string_view line, std::string_view &key, std::string_view &value)
{
  const std::size_t col = line.find(':');
  if(col == std::string_view::npos) return false;
  key = tr(line.substr(0, col));
  value = tr(line.substr(col + 1));
  return !key.empty();
}

inline std::string parse_leading_label(std::string_view &s)
{
  s = tr(s);
  if(s.empty() || s.front() == '[') return {};
  if(s.front() == '"' || s.front() == '\'')
  {
    const char quote = s.front();
    const std::size_t close = s.find(quote, 1);
    if(close == std::string_view::npos) return {};
    std::string label = strip_quotes(s.substr(0, close + 1));
    s = tr(s.substr(close + 1));
    return label;
  }
  const std::size_t br = find_unquoted(s, '[');
  const std::size_t ar = s.find("-->");
  if(ar != std::string_view::npos && (br == std::string_view::npos || ar < br))
  {
    std::string_view lhs = tr(s.substr(0, ar));
    const std::size_t sp = lhs.find_last_of(" \t");
    if(sp == std::string_view::npos) return {};
    std::string label = strip_quotes(tr(lhs.substr(0, sp)));
    s = tr(s.substr(sp + 1));
    return label;
  }
  std::size_t end = br == std::string_view::npos ? s.size() : br;
  std::string label = strip_quotes(tr(s.substr(0, end)));
  s = tr(s.substr(end));
  return label;
}

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

inline bool read_bracket_list(LineCursor &lines, std::string_view first, std::string &inner)
{
  inner.clear();
  const std::size_t open = find_unquoted(first, '[');
  if(open == std::string_view::npos) return false;

  std::string_view rest = first.substr(open + 1);
  while(true)
  {
    const std::size_t close = find_unquoted(rest, ']');
    if(close != std::string_view::npos)
    {
      inner += std::string(rest.substr(0, close));
      return true;
    }
    inner += std::string(rest);
    inner += '\n';
    std::string_view next_line;
    if(!lines.next(next_line)) return false;
    rest = next_line;
  }
}

} // namespace MermaidDiagrams
