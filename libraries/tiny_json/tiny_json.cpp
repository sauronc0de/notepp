#include "tiny_json.hpp"

#include <cstdlib>

namespace TinyJson
{
std::string json_escape(std::string_view s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for(char c : s)
  {
    switch(c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

std::string json_unescape(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for(size_t i = 0; i < s.size(); ++i)
  {
    if(s[i] == '\\' && i + 1 < s.size())
    {
      const char n = s[i + 1];
      if(n == 'n')
        out.push_back('\n');
      else if(n == 'r')
        out.push_back('\r');
      else if(n == 't')
        out.push_back('\t');
      else
        out.push_back(n);
      ++i;
    }
    else
    {
      out.push_back(s[i]);
    }
  }
  return out;
}

size_t find_matching(std::string_view s, size_t start, char open, char close)
{
  if(start >= s.size() || s[start] != open) return std::string::npos;
  int depth = 0;
  bool in_string = false;
  for(size_t i = start; i < s.size(); ++i)
  {
    const char c = s[i];
    if(c == '"' && (i == 0 || s[i - 1] != '\\')) in_string = !in_string;
    if(in_string) continue;
    if(c == open)
      ++depth;
    else if(c == close)
    {
      --depth;
      if(depth == 0) return i;
    }
  }
  return std::string::npos;
}

std::string json_find_string(std::string_view obj, std::string_view key)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  const size_t k = obj.find(pat);
  if(k == std::string::npos) return {};
  const size_t q1 = obj.find('"', k + pat.size());
  if(q1 == std::string::npos) return {};
  size_t q2 = q1 + 1;
  while(q2 < obj.size())
  {
    if(obj[q2] == '"' && obj[q2 - 1] != '\\') break;
    ++q2;
  }
  if(q2 >= obj.size()) return {};
  return json_unescape(obj.substr(q1 + 1, q2 - q1 - 1));
}

int json_find_int(std::string_view obj, std::string_view key, int defv)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  const size_t k = obj.find(pat);
  if(k == std::string::npos) return defv;
  const size_t c = obj.find(':', k + pat.size());
  if(c == std::string::npos) return defv;
  size_t b = c + 1;
  while(b < obj.size() && (obj[b] == ' ' || obj[b] == '\t' || obj[b] == '\n' || obj[b] == '\r')) ++b;
  size_t e = b;
  while(e < obj.size() && (obj[e] == '-' || (obj[e] >= '0' && obj[e] <= '9'))) ++e;
  if(e <= b) return defv;
  return std::atoi(std::string(obj.substr(b, e - b)).c_str());
}

bool json_find_bool(std::string_view obj, std::string_view key, bool defv)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  const size_t k = obj.find(pat);
  if(k == std::string::npos) return defv;
  const size_t c = obj.find(':', k + pat.size());
  if(c == std::string::npos) return defv;
  size_t b = c + 1;
  while(b < obj.size() && (obj[b] == ' ' || obj[b] == '\t' || obj[b] == '\n' || obj[b] == '\r')) ++b;
  if(obj.substr(b, 4) == "true") return true;
  if(obj.substr(b, 5) == "false") return false;
  return defv;
}

std::vector<std::string_view> json_array_objects(std::string_view arr)
{
  std::vector<std::string_view> out;
  size_t p = 0;
  while(p < arr.size())
  {
    const size_t b = arr.find('{', p);
    if(b == std::string::npos) break;
    const size_t e = find_matching(arr, b, '{', '}');
    if(e == std::string::npos) break;
    out.push_back(arr.substr(b, e - b + 1));
    p = e + 1;
  }
  return out;
}
} // namespace TinyJson

