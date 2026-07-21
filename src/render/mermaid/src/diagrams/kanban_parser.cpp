// ── kanban_parser.cpp ──────────────────────────────────────────────────────
//
// Kanban diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace kanbanparser
{
// Minimal replacement of `<br>` (and `<br/>`) with newlines inside a
// description. The kanban description is rendered as a tooltip; newlines
// inside it render as soft line breaks there.
static std::string br_to_newlines(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  while(!s.empty())
  {
    const auto pos = s.find("<br");
    if(pos == std::string_view::npos)
    {
      out.append(s.data(), s.size());
      break;
    }
    out.append(s.data(), pos);
    s.remove_prefix(pos + 3);
    // Accept `<br>`, `<br/>`, `<br />`.
    if(!s.empty() && s.front() == '/')
    {
      s.remove_prefix(1);
    }
    if(!s.empty() && s.front() == '>')
    {
      s.remove_prefix(1);
      out += '\n';
      continue;
    }
    // Not a recognised break; keep the literal `<br` and try again.
    out += "<br";
  }
  return out;
}

struct IndentLineCursor
{
  std::string_view src;
  std::size_t pos = 0;
  bool next(std::string_view &out, int &indent)
  {
    return _next(out, indent);
  }
  // Look at the next non-empty line without advancing the cursor, so the
  // caller can decide whether to consume it as part of a multi-line
  // description or treat it as a fresh card/column.
  bool peek(std::string_view &out, int &indent)
  {
    const std::size_t saved = pos;
    const bool ok = _next(out, indent);
    pos = saved;
    return ok;
  }

private:
  bool _next(std::string_view &out, int &indent)
  {
    while(pos < src.size())
    {
      std::size_t e = src.find('\n', pos);
      if(e == std::string_view::npos) e = src.size();
      std::string_view raw = src.substr(pos, e - pos);
      pos = (e < src.size()) ? e + 1 : e;
      std::size_t a = 0, b = raw.size();
      while(a < b && (raw[a] == ' ' || raw[a] == '\t' || raw[a] == '\r')) ++a;
      while(b > a && (raw[b - 1] == ' ' || raw[b - 1] == '\t' || raw[b - 1] == '\r')) --b;
      std::string_view t = raw.substr(a, b - a);
      if(t.empty()) continue;
      indent = 0;
      for(char c : raw)
      {
        if(c == ' ')
          indent++;
        else if(c == '\t')
          indent += 2;
        else
          break;
      }
      out = t;
      return true;
    }
    return false;
  }
};
} // namespace kanbanparser

bool parse_kanban(std::string_view src, KanbanDiagram &out)
{
  using namespace kanbanparser;
  out = KanbanDiagram{};
  IndentLineCursor L{src};
  std::string_view line;
  bool header = false;
  int indent = 0;
  int col_indent = -1;
  // Tracks the indent of the most recent card. Lines indented strictly
  // more than this are continuations of that card's description; we
  // join them with newlines so multi-line notes like
  //     c1[Title]: line one
  //       line two
  //       line three
  // round-trip into a single description instead of being misread as
  // three new nodes.
  int last_card_indent = -1;
  while(L.next(line, indent))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "kanban"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(line == "{" || line == "}") continue;
    if(!line.empty() && line.front() == '@') continue;
    std::string_view item_part = line;
    std::string desc;
    std::size_t rb = line.rfind(']');
    if(rb != std::string_view::npos)
    {
      std::size_t col = line.find(':', rb + 1);
      if(col != std::string_view::npos)
      {
        desc = br_to_newlines(tr(line.substr(col + 1)));
        item_part = line.substr(0, rb + 1);
      }
    }
    std::size_t b1 = item_part.find('[');
    std::size_t b2 = item_part.find(']');
    std::string id2, lbl;
    if(b1 != std::string_view::npos && b2 != std::string_view::npos)
    {
      id2 = std::string(tr(item_part.substr(0, b1)));
      lbl = std::string(tr(item_part.substr(b1 + 1, b2 - b1 - 1)));
    }
    else
    {
      id2 = std::string(item_part);
      lbl = id2;
    }
    if(id2.empty()) continue;
    if(col_indent < 0) col_indent = indent;
    if(indent <= col_indent)
    {
      out.columns.push_back({id2, lbl, {}});
      last_card_indent = -1;
      continue;
    }
    if(out.columns.empty()) continue;
    out.columns.back().cards.push_back({id2, lbl, desc});
    last_card_indent = indent;

    // Pull in continuation lines while they keep getting more indented
    // than the card itself. Two continuations on the same column are
    // accepted: as long as the line stays strictly past the card's
    // indent (set once when the card line was read), it merges into the
    // description. We deliberately do NOT bump `last_card_indent`
    // per-line, otherwise a second continuation at the same depth would
    // be misread as a fresh node.
    std::string_view peek_line;
    int peek_indent = 0;
    while(L.peek(peek_line, peek_indent) && peek_indent > last_card_indent)
    {
      auto &desc_field = out.columns.back().cards.back().description;
      if(!desc_field.empty()) desc_field += '\n';
      desc_field += br_to_newlines(tr(peek_line));
      L.next(peek_line, peek_indent);
    }
  }
  return header && !out.columns.empty();
}
} // namespace MermaidDiagrams
