// ── architecture_parser.cpp ────────────────────────────────────────────────
//
// Architecture diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace archparser
{

inline bool arch_strip_dir(std::string s, std::string &out)
{
  std::size_t col = s.rfind(':');
  if(col != std::string::npos)
  {
    std::string dir = lc(s.substr(col + 1));
    if(dir == "l" || dir == "r" || dir == "t" || dir == "b")
    {
      out = s.substr(0, col);
      return true;
    }
  }
  out = s;
  return false;
}

inline bool arch_split_arrow(std::string_view line, std::string_view &lhs, std::string_view &rhs, std::string &lbl)
{
  static const char *kOps[] = {"<-->", "-->", "<--", "-.->", "==>", "---"};
  std::size_t best = std::string_view::npos;
  std::size_t best_len = 0;
  for(const char *op : kOps)
  {
    std::size_t p = line.find(op);
    if(p != std::string_view::npos && (best == std::string_view::npos || p < best))
    {
      best = p;
      best_len = std::char_traits<char>::length(op);
    }
  }
  if(best == std::string_view::npos) return false;
  lhs = tr(line.substr(0, best));
  rhs = tr(line.substr(best + best_len));
  (void)lbl;
  return !lhs.empty() && !rhs.empty();
}

} // namespace archparser

bool parse_architecture(std::string_view src, ArchDiagram &out)
{
  using namespace archparser;
  out = ArchDiagram{};
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  std::unordered_map<std::string, int> sidx, gidx;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "architecture-beta") || sw(ll, "architecture"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "group "))
    {
      std::string_view rest = tr(line.substr(6));
      std::size_t p1 = rest.find('(');
      std::size_t p2 = rest.find(')');
      std::size_t b1 = rest.find('[');
      std::size_t b2 = rest.find(']');
      std::string gid = std::string(p1 != std::string_view::npos ? tr(rest.substr(0, p1)) : rest);
      std::string icon = (p1 != std::string_view::npos && p2 != std::string_view::npos)
                             ? std::string(rest.substr(p1 + 1, p2 - p1 - 1))
                             : "";
      std::string lbl = (b1 != std::string_view::npos && b2 != std::string_view::npos)
                            ? std::string(rest.substr(b1 + 1, b2 - b1 - 1))
                            : gid;
      int n = static_cast<int>(out.groups.size());
      out.groups.push_back({gid, icon, lbl});
      gidx[gid] = n;
      continue;
    }
    if(sw(ll, "service "))
    {
      std::string_view rest = tr(line.substr(8));
      std::size_t p1 = rest.find('(');
      std::size_t p2 = rest.find(')');
      std::size_t b1 = rest.find('[');
      std::size_t b2 = rest.find(']');
      std::string sid = std::string(p1 != std::string_view::npos ? tr(rest.substr(0, p1)) : rest);
      std::string icon = (p1 != std::string_view::npos && p2 != std::string_view::npos)
                             ? std::string(rest.substr(p1 + 1, p2 - p1 - 1))
                             : "";
      std::string lbl = (b1 != std::string_view::npos && b2 != std::string_view::npos)
                            ? std::string(rest.substr(b1 + 1, b2 - b1 - 1))
                            : sid;
      std::string grp = "";
      std::size_t in_pos = ll.find(" in ");
      if(in_pos != std::string::npos) grp = std::string(tr(line.substr(in_pos + 4)));
      int n = static_cast<int>(out.services.size());
      out.services.push_back({sid, icon, lbl, grp});
      sidx[sid] = n;
      continue;
    }
    std::string_view lhs, rhs;
    std::string lbl2;
    std::string f, t;
    bool has_arrow = arch_split_arrow(line, lhs, rhs, lbl2);
    bool has_qual = line.find(":L -- ") != std::string_view::npos ||
                    line.find(":R -- ") != std::string_view::npos;
    if(has_arrow || has_qual)
    {
      std::string raw_f(lhs);
      std::string raw_t(rhs);
      arch_strip_dir(raw_f, f);
      arch_strip_dir(raw_t, t);
      if(!f.empty() && !t.empty()) out.edges.push_back({f, t});
    }
  }
  return header && (!out.services.empty() || !out.groups.empty());
}
} // namespace MermaidDiagrams
