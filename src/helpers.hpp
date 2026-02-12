#pragma once

#include <string_view>

// Trim helpers
static inline std::string_view ltrim(std::string_view s)
{
  while(!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
    s.remove_prefix(1);
  return s;
}
static inline std::string_view rtrim(std::string_view s)
{
  while(!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}
static inline std::string_view trim(std::string_view s) { return rtrim(ltrim(s)); }

static inline bool starts_with(std::string_view s, std::string_view p)
{
  return s.size() >= p.size() && s.substr(0, p.size()) == p;
}