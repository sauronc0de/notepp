#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace MermaidDiagrams
{
namespace stateparser
{
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

struct StateLineCursor
{
  std::string_view src;
  std::size_t pos = 0;
  explicit StateLineCursor(std::string_view s) : src(s) {}
  bool next(std::string_view &out)
  {
    if(pos >= src.size()) return false;
    std::size_t e = src.find('\n', pos);
    if(e == std::string_view::npos) e = src.size();
    out = src.substr(pos, e - pos);
    pos = (e < src.size()) ? e + 1 : e;
    return true;
  }
};

static bool state_split_arrow(std::string_view line, std::string_view &lhs, std::string_view &rhs, std::string &lbl)
{
  const std::string_view arrow_chars = "<>-x)";
  std::size_t arrow_start = std::string_view::npos;
  for(std::size_t i = 0; i < line.size(); ++i)
  {
    if(line[i] == '-' || line[i] == '>' || line[i] == '<' || line[i] == 'x')
    {
      arrow_start = i;
      break;
    }
  }
  if(arrow_start == std::string_view::npos) return false;
  std::size_t arrow_end = arrow_start;
  while(arrow_end < line.size() && arrow_chars.find(line[arrow_end]) != std::string_view::npos) ++arrow_end;
  if(arrow_end == arrow_start) return false;
  lhs = tr(line.substr(0, arrow_start));
  std::string_view rest = tr(line.substr(arrow_end));
  auto colon = rest.find(':');
  if(colon != std::string_view::npos)
  {
    rhs = tr(rest.substr(0, colon));
    lbl = std::string(tr(rest.substr(colon + 1)));
  }
  else
  {
    rhs = rest;
  }
  return !lhs.empty() && !rhs.empty();
}
} // namespace stateparser

bool parse_state(std::string_view src, StateDiagram &out)
{
  using namespace stateparser;
  out = StateDiagram{};
  std::unordered_map<std::string, int> sidx;
  auto ensure_state = [&](const std::string &id, const std::string &lbl = "") {
    auto it = sidx.find(id);
    if(it != sidx.end()) return it->second;
    StateNode s;
    s.id = id;
    s.label = lbl.empty() ? id : lbl;
    s.is_start = (id == "[*]" && out.states.empty());
    s.is_end = (id == "[*]" && !out.states.empty());
    int n = static_cast<int>(out.states.size());
    out.states.push_back(s);
    sidx[id] = n;
    return n;
  };

  StateLineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header) { if(sw(ll, "statediagram")) { header = true; continue; } continue; }
    if(ll == "state {" || ll == "}") continue;
    if(sw(ll, "state "))
    {
      std::string_view rest = tr(line.substr(6));
      std::size_t as = lc(rest).find(" as ");
      std::string id2(as != std::string_view::npos ? tr(rest.substr(as + 4)) : rest);
      std::string lbl(as != std::string_view::npos ? tr(rest.substr(0, as)) : rest);
      if(!id2.empty()) ensure_state(id2, lbl);
      continue;
    }
    std::string_view lhs, rhs;
    std::string lbl2;
    if(state_split_arrow(line, lhs, rhs, lbl2))
    {
      std::string f(lhs);
      std::string t(rhs);
      int fi = ensure_state(f);
      int ti = ensure_state(t);
      if(t == "[*]") { out.states[ti].is_end = true; out.states[ti].is_start = false; }
      out.transitions.push_back({f, t, lbl2});
      (void)fi;
    }
  }
  return header && !out.states.empty();
}
} // namespace MermaidDiagrams