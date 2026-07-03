#include "mermaid_diagrams.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace MermaidDiagrams
{
namespace classparser
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

struct ClassLineCursor
{
  std::string_view src;
  std::size_t pos = 0;
  explicit ClassLineCursor(std::string_view s) : src(s) {}
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
} // namespace classparser

bool parse_class(std::string_view src, ClassDiagram &out)
{
  using namespace classparser;
  out = ClassDiagram{};
  std::unordered_map<std::string, int> cidx;
  auto ensure_cls = [&](const std::string &name) {
    auto it = cidx.find(name);
    if(it != cidx.end()) return it->second;
    ClassDef c;
    c.name = name;
    int n = static_cast<int>(out.classes.size());
    out.classes.push_back(c);
    cidx[name] = n;
    return n;
  };

  ClassLineCursor L{src};
  std::string_view line;
  bool header = false;
  std::string cur_class;
  while(L.next(line))
  {
    std::string_view tline = tr(line);
    std::string ll = lc(tline);
    if(!header)
    {
      if(sw(ll, "classdiagram"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "class "))
    {
      std::string_view rest = tr(tline.substr(6));
      std::size_t brace = rest.find('{');
      std::string name = std::string(brace != std::string_view::npos ? tr(rest.substr(0, brace)) : rest);
      std::size_t sq = name.find("<<");
      if(sq != std::string_view::npos) name = std::string(tr(name.substr(0, sq)));
      cur_class = name;
      ensure_cls(name);
      continue;
    }
    if(tline == "{") continue;
    if(tline == "}")
    {
      cur_class.clear();
      continue;
    }
    if(!cur_class.empty() && sw(tline, "<<") && tline.find(">>") != std::string_view::npos)
    {
      std::size_t e2 = tline.find(">>");
      std::size_t sb = tline.find("<<");
      out.classes[cidx[cur_class]].stereotype = std::string(tr(tline.substr(sb + 2, e2 - sb - 2)));
      continue;
    }
    if(!cur_class.empty() && !tline.empty())
    {
      char vis = '+';
      std::string_view ml = tline;
      if(ml[0] == '+' || ml[0] == '-' || ml[0] == '#' || ml[0] == '~')
      {
        vis = ml[0];
        ml = tr(ml.substr(1));
      }
      bool is_m = ml.find('(') != std::string_view::npos;
      ClassMember cm;
      cm.vis = vis;
      cm.is_method = is_m;
      cm.name = std::string(ml);
      out.classes[cidx[cur_class]].members.push_back(cm);
      continue;
    }
    {
      std::string_view lhs, rhs;
      std::string lbl;
      static const char *rels[] = {"<|--", "<|..", "*--", "o--", "-->", "..>", "--", nullptr};
      std::size_t best = std::string_view::npos;
      std::size_t best_len = 0;
      int best_ri = -1;
      for(int ri = 0; rels[ri]; ++ri)
      {
        std::size_t p = tline.find(rels[ri]);
        if(p != std::string_view::npos && (best == std::string_view::npos || p < best))
        {
          best = p;
          best_len = std::strlen(rels[ri]);
          best_ri = ri;
        }
      }
      if(best != std::string_view::npos)
      {
        lhs = tr(tline.substr(0, best));
        rhs = tr(tline.substr(best + best_len));
        std::size_t col = rhs.rfind(':');
        if(col != std::string_view::npos)
        {
          lbl = std::string(tr(rhs.substr(col + 1)));
          rhs = tr(rhs.substr(0, col));
        }
        std::string f(lhs);
        std::string t(rhs);
        if(f.empty() || t.empty()) continue;
        ensure_cls(f);
        ensure_cls(t);
        ClassRel::T rt = ClassRel::T::Link;
        if(best_ri == 0 || best_ri == 1)
          rt = ClassRel::T::Inheritance;
        else if(best_ri == 2)
          rt = ClassRel::T::Composition;
        else if(best_ri == 3)
          rt = ClassRel::T::Aggregation;
        else if(best_ri == 4)
          rt = ClassRel::T::Association;
        else if(best_ri == 5)
          rt = ClassRel::T::Dependency;
        out.relations.push_back({f, t, lbl, "", "", rt});
      }
    }
  }
  return header && !out.classes.empty();
}
} // namespace MermaidDiagrams