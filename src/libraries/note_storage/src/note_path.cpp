#include "note_path.hpp"

#include "string_utils.hpp"

#include <algorithm>
#include <vector>

namespace notepp::note_storage
{
std::filesystem::path make_note_path(const std::filesystem::path &data_root,
                                     std::string_view folder_name,
                                     std::string_view note_title)
{
  std::string f;
  if(!folder_name.empty())
  {
    size_t p = 0;
    bool first = true;
    while(p <= folder_name.size())
    {
      size_t s = folder_name.find('/', p);
      if(s == std::string_view::npos) s = folder_name.size();
      std::string seg = StringUtils::sanitize_note_filename(std::string(folder_name.substr(p, s - p)));
      if(!seg.empty() && seg != "note")
      {
        if(!first) f += "/";
        f += seg;
        first = false;
      }
      if(s == folder_name.size()) break;
      p = s + 1;
    }
  }
  const std::string n = StringUtils::sanitize_note_filename(std::string(note_title));
  if(n == "note" && folder_name.empty())
    return data_root / "note.md";
  if(f.empty())
    return data_root / (n + ".md");
  return (data_root / f) / (n + ".md");
}

std::string make_unique_note_title(const std::vector<std::string> &existing_titles,
                                   std::string_view base_title)
{
  std::string base = StringUtils::sanitize_note_filename(base_title.empty() ? std::string("Note") : std::string(base_title));
  std::string candidate = base;
  int suffix = 2;
  auto exists = [&](const std::string &title) {
    for(const auto &t : existing_titles)
      if(t == title) return true;
    return false;
  };
  while(exists(candidate))
    candidate = base + " " + std::to_string(suffix++);
  return candidate;
}
} // namespace notepp::note_storage