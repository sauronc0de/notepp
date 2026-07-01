#include "mermaid_diagrams.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace MermaidDiagrams
{
namespace seqparser
{
static bool starts_with(std::string_view s, std::string_view p)
{
  if(s.size() < p.size()) return false;
  for(std::size_t i = 0; i < p.size(); ++i)
    if(s[i] != p[i]) return false;
  return true;
}

static std::string_view trim(std::string_view s)
{
  std::size_t a = 0, b = s.size();
  while(a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while(b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

static std::string lower(std::string_view s)
{
  std::string out(s);
  for(char &c : out)
    if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

struct LineCursor
{
  std::string_view src;
  std::size_t pos = 0;
  explicit LineCursor(std::string_view s) : src(s) {}
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

static bool seq_split_arrow(std::string_view line, std::string_view &lhs, std::string_view &rhs, std::string &lbl)
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
  lhs = trim(line.substr(0, arrow_start));
  std::string_view rest = trim(line.substr(arrow_end));
  auto colon = rest.find(':');
  if(colon != std::string_view::npos)
  {
    rhs = trim(rest.substr(0, colon));
    lbl = std::string(trim(rest.substr(colon + 1)));
  }
  else
  {
    rhs = rest;
  }
  return !lhs.empty() && !rhs.empty();
}
} // namespace seqparser

bool parse_sequence(std::string_view src, SequenceDiagram &out)
{
  using namespace seqparser;
  out = SequenceDiagram{};
  std::unordered_map<std::string, int> pidx;
  auto ensure_part = [&](const std::string &id, const std::string &lbl = "") {
    auto it = pidx.find(id);
    if(it != pidx.end()) return it->second;
    SeqParticipant p;
    p.id = id;
    p.label = lbl.empty() ? id : lbl;
    int n = static_cast<int>(out.participants.size());
    out.participants.push_back(p);
    pidx[id] = n;
    return n;
  };

  LineCursor L{src};
  std::string_view line;
  bool header = false;
  while(L.next(line))
  {
    std::string_view tline = trim(line);
    std::string ll = lower(tline);
    if(!header)
    {
      if(starts_with(ll, "sequencediagram")) { header = true; continue; }
      continue;
    }
    if(starts_with(ll, "title ")) { out.title = std::string(trim(tline.substr(6))); continue; }
    if(starts_with(ll, "participant ") || starts_with(ll, "actor "))
    {
      bool actor = starts_with(ll, "actor ");
      std::string_view rest = trim(tline.substr(actor ? 6 : 12));
      std::string id_s(rest);
      std::string lbl_s = id_s;
      std::size_t as = rest.find(" as ");
      if(as != std::string_view::npos)
      {
        id_s = std::string(trim(rest.substr(0, as)));
        lbl_s = std::string(trim(rest.substr(as + 4)));
      }
      int n = ensure_part(id_s, lbl_s);
      out.participants[n].is_actor = actor;
      continue;
    }
    if(starts_with(ll, "note "))
    {
      std::size_t col = line.find(':');
      std::string text = (col != std::string_view::npos) ? std::string(trim(line.substr(col + 1))) : "";
      std::string_view spec = (col != std::string_view::npos) ? trim(line.substr(5, col - 5)) : trim(line.substr(5));
      SeqNote note;
      note.text = text;
      if(starts_with(lower(spec), "over "))
      {
        std::string_view ids = trim(spec.substr(5));
        std::size_t c = ids.find(',');
        note.over1 = std::string(c != std::string_view::npos ? trim(ids.substr(0, c)) : ids);
        note.over2 = c != std::string_view::npos ? std::string(trim(ids.substr(c + 1))) : "";
      }
      else
      {
        note.over1 = std::string(spec);
      }
      ensure_part(note.over1);
      if(!note.over2.empty()) ensure_part(note.over2);
      int ni = static_cast<int>(out.notes.size());
      out.notes.push_back(note);
      out.events.push_back({SequenceDiagram::Event::T::Note, ni, "", "", ""});
      continue;
    }
    if(starts_with(ll, "activate "))
    {
      std::string a = std::string(trim(tline.substr(9)));
      ensure_part(a);
      out.events.push_back({SequenceDiagram::Event::T::Activate, -1, "", "", a});
      continue;
    }
    if(starts_with(ll, "deactivate "))
    {
      std::string a = std::string(trim(tline.substr(11)));
      ensure_part(a);
      out.events.push_back({SequenceDiagram::Event::T::Deactivate, -1, "", "", a});
      continue;
    }
    for(auto *kw : {"loop ", "alt ", "opt ", "par ", "break ", "critical "})
    {
      if(starts_with(ll, kw))
      {
        std::size_t kwlen = std::strlen(kw);
        out.events.push_back({SequenceDiagram::Event::T::GroupStart, -1,
                              std::string(trim(tline.substr(kwlen))),
                              std::string(kw).substr(0, kwlen - 1), ""});
        goto next_line;
      }
    }
    if(ll == "end")
    {
      out.events.push_back({SequenceDiagram::Event::T::GroupEnd, -1, "", "", ""});
      continue;
    }
    {
      std::string_view lhs, rhs;
      std::string lbl;
      if(seq_split_arrow(tline, lhs, rhs, lbl))
      {
        std::string from = std::string(trim(lhs));
        std::string to = std::string(trim(rhs));
        if(!to.empty() && to.back() == '+') { to.pop_back(); out.events.push_back({SequenceDiagram::Event::T::Activate, -1, "", "", to}); }
        if(!to.empty() && to.back() == '-') { to.pop_back(); out.events.push_back({SequenceDiagram::Event::T::Deactivate, -1, "", "", to}); }
        ensure_part(from);
        ensure_part(to);
        bool dotted = line.find("--") != std::string_view::npos;
        bool open = line.find(">>") != std::string_view::npos || line.find(">") != std::string_view::npos;
        SeqMessage msg{from, to, lbl, dotted, open};
        int mi = static_cast<int>(out.messages.size());
        out.messages.push_back(msg);
        out.events.push_back({SequenceDiagram::Event::T::Message, mi, "", "", ""});
      }
    }
  next_line:;
  }
  return header && !out.participants.empty();
}
} // namespace MermaidDiagrams