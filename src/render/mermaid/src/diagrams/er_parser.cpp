// ── er_parser.cpp ──────────────────────────────────────────────────────────
//
// ER (entity-relationship) diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace MermaidDiagrams
{
namespace erparser
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
} // namespace erparser

bool parse_er(std::string_view src, ERDiagram &out)
{
  using namespace erparser;
  out = ERDiagram{};
  std::unordered_map<std::string, int> eidx;
  auto ensure_entity = [&](const std::string &name) {
    auto it = eidx.find(name);
    if(it != eidx.end()) return it->second;
    EREntity e;
    e.name = name;
    int n = static_cast<int>(out.entities.size());
    out.entities.push_back(e);
    eidx[name] = n;
    return n;
  };

  LineCursor L{src};
  std::string_view line;
  bool header = false;
  std::string cur_entity;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "erdiagram"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(line == "{") continue;
    if(line == "}")
    {
      cur_entity.clear();
      continue;
    }

    if(!cur_entity.empty())
    {
      // attribute line: type name [PK|FK]
      std::string_view parts = line;
      std::size_t sp = parts.find(' ');
      std::string type = sp != std::string_view::npos ? std::string(parts.substr(0, sp)) : "field";
      std::string rest2 = sp != std::string_view::npos ? std::string(tr(parts.substr(sp))) : "";
      bool pk = lc(line).find("pk") != std::string::npos;
      bool fk = lc(line).find("fk") != std::string::npos;
      std::string name2 = rest2;
      std::size_t pkp = lc(name2).find(" pk");
      if(pkp != std::string::npos) name2 = name2.substr(0, pkp);
      std::size_t fkp = lc(name2).find(" fk");
      if(fkp != std::string::npos) name2 = name2.substr(0, fkp);
      name2 = std::string(tr(name2));
      name2 = strip_quotes(name2);
      out.entities[eidx[cur_entity]].attrs.push_back({type, name2, pk, fk});
      continue;
    }

    // relation line: detect -- or ||
    if(line.find("--") != std::string_view::npos || line.find("||") != std::string_view::npos)
    {
      std::size_t col = line.rfind(':');
      std::string lbl2 = col != std::string_view::npos ? std::string(tr(line.substr(col + 1))) : "";
      std::string body = col != std::string_view::npos ? std::string(tr(line.substr(0, col))) : std::string(line);
      std::size_t s1 = body.find(' ');
      if(s1 == std::string::npos) goto try_entity;
      std::string e1 = body.substr(0, s1);
      std::size_t s2 = body.rfind(' ');
      if(s2 == s1) goto try_entity;
      std::string e2 = body.substr(s2 + 1);
      (void)tr(body.substr(s1, s2 - s1));
      ensure_entity(e1);
      ensure_entity(e2);
      out.relations.push_back({e1, e2, lbl2, "", ""});
      continue;
    }

  try_entity: {
    std::size_t brace = line.find('{');
    std::string ename = std::string(brace != std::string_view::npos ? tr(line.substr(0, brace)) : line);
    if(!ename.empty() && ename.find(' ') == std::string::npos)
    {
      ensure_entity(ename);
      cur_entity = ename;
    }
  }
  }
  return header && !out.entities.empty();
}
} // namespace MermaidDiagrams
