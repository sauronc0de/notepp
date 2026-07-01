#include "string_utils.hpp"

#include <algorithm>
#include <cctype>

namespace StringUtils
{
std::string_view ltrim(std::string_view s)
{
  while(!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
    s.remove_prefix(1);
  return s;
}

std::string_view rtrim(std::string_view s)
{
  while(!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}

std::string_view trim(std::string_view s)
{
  return rtrim(ltrim(s));
}

bool starts_with(std::string_view s, std::string_view prefix)
{
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::string sanitize_note_filename(std::string title)
{
  for(char &c : title)
  {
    const bool bad =
        c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|';
    if(bad) c = '_';
  }

  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while(!title.empty() && is_space(title.front())) title.erase(title.begin());
  while(!title.empty() && is_space(title.back())) title.pop_back();
  if(title.empty()) title = "note";
  return title;
}

std::string to_lower_copy(std::string_view s)
{
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

float clamp01f(float v)
{
  if(v < 0.0f) return 0.0f;
  if(v > 1.0f) return 1.0f;
  return v;
}
} // namespace StringUtils

