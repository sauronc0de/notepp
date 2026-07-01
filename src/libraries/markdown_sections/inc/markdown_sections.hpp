#pragma once

#include <string>
#include <string_view>
#include <vector>

struct MdSection
{
  int level = 0;
  std::string title;
  std::string body;
  std::vector<MdSection> kids;
};

bool parse_heading_line(std::string_view line, int &level_out, std::string_view &title_out);
MdSection parse_sections(std::string_view md);
