#include "markdown_images.hpp"

#include "string_utils.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

namespace MarkdownImages
{
bool is_external_link(std::string_view href)
{
  return StringUtils::starts_with(href, "http://") || StringUtils::starts_with(href, "https://");
}

std::string decode_link_component(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for(std::size_t i = 0; i < s.size(); ++i)
  {
    if(s[i] == '%' && i + 2 < s.size())
    {
      auto hex = [](char c) -> int {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
      };
      const int hi = hex(s[i + 1]);
      const int lo = hex(s[i + 2]);
      if(hi >= 0 && lo >= 0)
      {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    if(s[i] == '+')
      out.push_back(' ');
    else
      out.push_back(s[i]);
  }
  return out;
}

std::filesystem::path resolve_image_path(std::string_view href,
                                         const std::filesystem::path &assets_root,
                                         const std::filesystem::path &document_dir)
{
  const std::filesystem::path p(href);
  if(p.is_absolute() && std::filesystem::exists(p)) return p;

  const std::filesystem::path repo_root = assets_root.parent_path();

  std::vector<std::filesystem::path> candidates;
  candidates.push_back(p);

  if(!document_dir.empty())
    candidates.push_back(document_dir / p);

  candidates.push_back(repo_root / p);
  candidates.push_back(assets_root / p);

  if(StringUtils::starts_with(href, "assets/"))
  {
    const std::filesystem::path rel = p.lexically_relative("assets");
    candidates.push_back(assets_root / rel);
  }

  for(const auto &c : candidates)
  {
    std::error_code ec;
    if(std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec))
      return c;
  }
  return {};
}
} // namespace MarkdownImages