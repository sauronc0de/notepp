#include "markdown_sections.hpp"

#include "helpers.hpp"

bool parse_heading_line(std::string_view line, int &level_out, std::string_view &title_out)
{
  line = ltrim(line);
  int level = 0;
  while(level < 6 && level < (int)line.size() && line[(size_t)level] == '#') level++;
  if(level == 0) return false;

  if((size_t)level >= line.size() || line[(size_t)level] != ' ') return false;

  std::string_view title = trim(line.substr((size_t)level + 1));
  if(title.empty()) title = "(untitled)";

  level_out = level;
  title_out = title;
  return true;
}

MdSection parse_sections(std::string_view md)
{
  MdSection root;
  std::vector<MdSection *> stack;
  stack.push_back(&root);

  size_t pos = 0;
  auto take_line = [&](size_t &p) -> std::string_view {
    if(p >= md.size()) return {};
    size_t e = md.find('\n', p);
    if(e == std::string_view::npos) e = md.size();
    std::string_view line = md.substr(p, e - p);
    p = (e < md.size()) ? e + 1 : e;
    return line;
  };

  while(pos < md.size())
  {
    std::string_view line = take_line(pos);

    int level = 0;
    std::string_view title;
    if(parse_heading_line(line, level, title))
    {
      while(!stack.empty() && stack.back()->level >= level) stack.pop_back();
      if(stack.empty()) stack.push_back(&root);

      stack.back()->kids.push_back(MdSection{level, std::string(title), {}, {}});
      MdSection *added = &stack.back()->kids.back();
      stack.push_back(added);
    }
    else
    {
      stack.back()->body.append(line.data(), line.size());
      stack.back()->body.push_back('\n');
    }
  }

  return root;
}
