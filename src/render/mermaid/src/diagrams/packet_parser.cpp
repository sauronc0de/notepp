// ── packet_parser.cpp ──────────────────────────────────────────────────────
//
// Packet diagram parser for the mermaid library.
// Extracted from mermaid_diagrams.cpp.

#include "mermaid_diagrams.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "parser_helpers.hpp"

namespace MermaidDiagrams
{
namespace packetparser
{

static void pkt_parse_config(std::string_view src, PacketConfig &cfg)
{
  const std::string s(src);
  std::size_t pos = 0;
  while((pos = s.find("%%{", pos)) != std::string::npos)
  {
    std::size_t end = s.find("%%", pos + 3);
    if(end == std::string::npos) break;
    const std::string dir = s.substr(pos + 3, end - pos - 3);
    std::size_t pk = dir.find("packet");
    if(pk != std::string::npos)
    {
      std::size_t colon = dir.find(':', pk + 6);
      std::size_t ob = (colon != std::string::npos) ? dir.find('{', colon) : std::string::npos;
      std::size_t cb = (ob != std::string::npos) ? dir.find('}', ob + 1) : std::string::npos;
      if(cb != std::string::npos)
      {
        const std::string inner = dir.substr(ob + 1, cb - ob - 1);
        auto gf = [&](const char *k, float &v) {
          std::size_t kp = inner.find(k);
          if(kp == std::string::npos) return;
          std::size_t cp = inner.find(':', kp + std::strlen(k));
          if(cp == std::string::npos) return;
          float r = static_cast<float>(std::atof(inner.c_str() + cp + 1));
          if(r > 0) v = r;
        };
        auto gi = [&](const char *k, int &v) {
          std::size_t kp = inner.find(k);
          if(kp == std::string::npos) return;
          std::size_t cp = inner.find(':', kp + std::strlen(k));
          if(cp == std::string::npos) return;
          int r = std::atoi(inner.c_str() + cp + 1);
          if(r > 0) v = r;
        };
        auto gb = [&](const char *k, bool &v) {
          std::size_t kp = inner.find(k);
          if(kp == std::string::npos) return;
          std::size_t cp = inner.find(':', kp + std::strlen(k));
          if(cp == std::string::npos) return;
          const std::string rest = inner.substr(cp + 1);
          std::size_t tp = rest.find("true");
          std::size_t fp2 = rest.find("false");
          if(tp != std::string::npos && (fp2 == std::string::npos || tp < fp2))
            v = true;
          else if(fp2 != std::string::npos)
            v = false;
        };
        gf("bitWidth", cfg.bitWidth);
        gf("rowHeight", cfg.rowHeight);
        gi("bitsPerRow", cfg.bitsPerRow);
        gb("showBits", cfg.showBits);
        gf("paddingX", cfg.paddingX);
        gf("paddingY", cfg.paddingY);
        gb("showLegend", cfg.showLegend);
      }
    }
    pos = end + 2;
  }
}

} // namespace packetparser

bool parse_packet(std::string_view src, PacketDiagram &out)
{
  using namespace packetparser;
  out = PacketDiagram{};
  pkt_parse_config(src, out.config);
  LineCursor L{src};
  std::string_view line;
  bool header = false;
  int cur = 0;
  while(L.next(line))
  {
    std::string ll = lc(line);
    if(!header)
    {
      if(sw(ll, "packet-beta") || sw(ll, "packet"))
      {
        header = true;
        continue;
      }
      continue;
    }
    if(sw(ll, "%%") || sw(ll, "//")) continue;
    if(sw(ll, "title "))
    {
      out.title = strip_quotes(line.substr(6));
      continue;
    }
    std::size_t col = line.find(':');
    if(col == std::string_view::npos) continue;
    std::string range_s = std::string(tr(line.substr(0, col)));
    std::string name = strip_quotes(line.substr(col + 1));
    int start = 0, end = 0;
    std::size_t dash = range_s.find('-');
    std::size_t plus = range_s.find('+');
    if(dash != std::string::npos)
    {
      start = std::atoi(range_s.substr(0, dash).c_str());
      end = std::atoi(range_s.substr(dash + 1).c_str());
    }
    else if(plus == 0)
    {
      start = cur;
      end = cur + std::atoi(range_s.c_str() + 1) - 1;
    }
    else if(plus != std::string::npos)
    {
      start = std::atoi(range_s.substr(0, plus).c_str());
      end = start + std::atoi(range_s.substr(plus + 1).c_str()) - 1;
    }
    else
    {
      start = end = std::atoi(range_s.c_str());
    }
    cur = end + 1;
    out.total_bits = std::max(out.total_bits, end + 1);
    out.fields.push_back({start, end, name});
  }
  return header && !out.fields.empty();
}
} // namespace MermaidDiagrams
