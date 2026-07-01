// ── requirement_parser.cpp ─────────────────────────────────────────────────
//
// Requirement diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <cstring>
#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace requirementparser
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
} // namespace requirementparser

bool parse_requirement(std::string_view src, RequirementDiagram &out)
{
  using namespace requirementparser;
  out = RequirementDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  std::string cur_block_type, cur_name;
  Requirement cur_req;
  ReqElement cur_elem;
  bool in_req = false, in_elem = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "requirementdiagram"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(line == "}")
    {
      if(in_req)
      {
        out.reqs.push_back(cur_req);
        in_req = false;
      }
      if(in_elem)
      {
        out.elements.push_back(cur_elem);
        in_elem = false;
      }
      continue;
    }
    if(in_req)
    {
      if(sw(ll, "id: ")) cur_req.id = std::string(tr(line.substr(4)));
      else if(sw(ll, "text: ")) cur_req.text = std::string(tr(line.substr(6)));
      else if(sw(ll, "risk: ")) cur_req.risk = std::string(tr(line.substr(6)));
      else if(sw(ll, "verifymethod: ") || sw(ll, "verify: ")) cur_req.method = std::string(tr(line.substr(ll.find(':') + 2)));
      continue;
    }
    if(in_elem)
    {
      if(sw(ll, "type: ")) cur_elem.type = std::string(tr(line.substr(6)));
      else if(sw(ll, "docref: ")) cur_elem.docref = std::string(tr(line.substr(8)));
      continue;
    }
    // relation: A - satisfies -> B
    if(line.find(" - ") != std::string_view::npos && line.find(" -> ") != std::string_view::npos)
    {
      std::size_t d1 = line.find(" - ");
      std::size_t d2 = line.find(" -> ");
      std::string f = std::string(tr(line.substr(0, d1)));
      std::string rel = std::string(tr(line.substr(d1 + 3, d2 - d1 - 3)));
      std::string t = std::string(tr(line.substr(d2 + 4)));
      out.relations.push_back({f, t, rel});
      continue;
    }
    // block start: requirementType name {
    for(auto kw : {"requirement", "functionalrequirement", "interfacerequirement", "performancerequirement", "physicalrequirement", "designconstraint"})
    {
      if(sw(ll, kw) && (ll.size() == std::strlen(kw) || ll[std::strlen(kw)] == ' '))
      {
        cur_req = Requirement{};
        cur_req.type = kw;
        std::string_view rest2 = tr(line.substr(std::strlen(kw)));
        std::size_t brace = rest2.find('{');
        cur_req.name = std::string(brace != std::string_view::npos ? tr(rest2.substr(0, brace)) : rest2);
        in_req = true;
        goto next_rq;
      }
    }
    if(sw(ll, "element "))
    {
      cur_elem = ReqElement{};
      std::string_view rest2 = tr(line.substr(8));
      std::size_t brace = rest2.find('{');
      cur_elem.name = std::string(brace != std::string_view::npos ? tr(rest2.substr(0, brace)) : rest2);
      in_elem = true;
    }
  next_rq:;
  }
  return header && (!out.reqs.empty() || !out.elements.empty());
}
} // namespace MermaidDiagrams
