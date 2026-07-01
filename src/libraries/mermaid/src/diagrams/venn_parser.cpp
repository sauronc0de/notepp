// ── venn_parser.cpp ────────────────────────────────────────────────────────
//
// Venn diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <sstream>
#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace vennparser
{
using VennLineCursor = LineCursor;
} // namespace vennparser

bool parse_venn(std::string_view src, VennDiagram &out)
{
  using namespace vennparser;
  out = VennDiagram{};
  VennLineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "venn"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "title "))
    {
      out.title = strip_quotes(line.substr(6));
      continue;
    }
    std::size_t amp = line.find('&');
    std::size_t plus2 = line.find('+');
    std::size_t sep = (amp != std::string_view::npos) ? amp
                     : ((plus2 != std::string_view::npos) ? plus2 : std::string_view::npos);
    if(sep != std::string_view::npos)
    {
      std::string ids = std::string(tr(line));
      std::size_t q1 = ids.find('"');
      std::size_t q2 = ids.rfind('"');
      std::string lbl = (q1 != std::string::npos && q2 > q1) ? ids.substr(q1 + 1, q2 - q1 - 1) : "";
      std::string id_part = ids.substr(0, q1 != std::string::npos ? q1 : ids.size());
      VennIntersection vi;
      vi.label = lbl;
      std::istringstream ss2(id_part);
      std::string tok;
      while(std::getline(ss2, tok, amp != std::string_view::npos ? '&' : '+'))
        vi.set_ids.push_back(std::string(tr(tok)));
      out.intersections.push_back(vi);
    }
    else
    {
      std::string ls(line);
      std::size_t q1 = ls.find('"');
      std::size_t q2 = ls.rfind('"');
      std::string set_id = q1 != std::string::npos ? std::string(tr(ls.substr(0, q1))) : ls;
      std::string lbl2 = (q1 != std::string::npos && q2 > q1) ? ls.substr(q1 + 1, q2 - q1 - 1) : set_id;
      set_id = std::string(tr(set_id));
      if(!set_id.empty()) out.sets.push_back({set_id, lbl2});
    }
  }
  return header && !out.sets.empty();
}
} // namespace MermaidDiagrams
