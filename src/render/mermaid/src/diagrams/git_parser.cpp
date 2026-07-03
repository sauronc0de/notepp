// ── git_parser.cpp ─────────────────────────────────────────────────────────
//
// Git graph diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace MermaidDiagrams
{
namespace gitparser
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
} // namespace gitparser

bool parse_git(std::string_view src, GitDiagram &out)
{
  using namespace gitparser;
  out = GitDiagram{};
  out.main_branch = "main";
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  std::string cur_branch = "main";
  out.branches.push_back("main");
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "gitgraph"))
      {
        header = true;
        std::string_view rest = tr(line.substr(9 < line.size() ? 9 : line.size()));
        if(!rest.empty())
        {
          out.main_branch = std::string(rest);
          out.branches[0] = out.main_branch;
          cur_branch = out.main_branch;
        }
        continue;
      }
      continue;
    }
    if(sw(ll, "commit"))
    {
      GitCommit c;
      c.branch = cur_branch;
      std::size_t id_pos = ll.find("id:");
      if(id_pos != std::string::npos)
      {
        std::string_view rest2 = tr(line.substr(id_pos + 3));
        c.id = strip_quotes(rest2.substr(0, rest2.find_first_of(" ,\"") == std::string_view::npos ? rest2.size() : rest2.find_first_of(" ,\"")));
      }
      std::size_t tag_pos = ll.find("tag:");
      if(tag_pos != std::string::npos)
      {
        std::string_view rest2 = tr(line.substr(tag_pos + 4));
        c.tag = strip_quotes(rest2.substr(0, rest2.find_first_of(" ,\"") == std::string_view::npos ? rest2.size() : rest2.find_first_of(" ,\"")));
      }
      if(ll.find("type:reverse") != std::string::npos) c.type = GitCommit::T::Reverse;
      if(ll.find("type:highlight") != std::string::npos) c.type = GitCommit::T::Highlight;
      out.commits.push_back(c);
      continue;
    }
    if(sw(ll, "branch "))
    {
      std::string bn = std::string(tr(line.substr(7)));
      if(std::find(out.branches.begin(), out.branches.end(), bn) == out.branches.end())
        out.branches.push_back(bn);
      cur_branch = bn;
      continue;
    }
    if(sw(ll, "checkout "))
    {
      cur_branch = std::string(tr(line.substr(9)));
      continue;
    }
    if(sw(ll, "merge "))
    {
      std::string from_b = std::string(tr(line.substr(6)));
      GitCommit c;
      c.branch = cur_branch;
      c.is_merge = true;
      c.merge_from = from_b;
      out.commits.push_back(c);
      continue;
    }
    if(sw(ll, "cherry-pick"))
    {
      GitCommit c;
      c.branch = cur_branch;
      out.commits.push_back(c);
      continue;
    }
  }
  return header;
}
} // namespace MermaidDiagrams
