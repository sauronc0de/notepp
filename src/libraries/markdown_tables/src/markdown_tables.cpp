#include "markdown_tables.hpp"

#include "string_utils.hpp"

namespace MarkdownTables
{
std::vector<std::string> split_md_table_cells(std::string_view line)
{
  std::vector<std::string> cells;
  std::string_view t = StringUtils::trim(line);
  if(t.empty() || t.find('|') == std::string_view::npos) return cells;

  if(!t.empty() && t.front() == '|') t.remove_prefix(1);
  if(!t.empty() && t.back() == '|') t.remove_suffix(1);

  std::size_t start = 0;
  while(start <= t.size())
  {
    std::size_t sep = t.find('|', start);
    const std::size_t end = (sep == std::string_view::npos) ? t.size() : sep;
    cells.emplace_back(StringUtils::trim(t.substr(start, end - start)));
    if(sep == std::string_view::npos) break;
    start = sep + 1;
  }
  return cells;
}

bool is_md_table_separator(std::string_view line, std::size_t expected_cols)
{
  const std::vector<std::string> parts = split_md_table_cells(line);
  if(parts.size() != expected_cols || parts.empty()) return false;

  for(const std::string &p : parts)
  {
    std::string_view s = StringUtils::trim(p);
    if(s.empty()) return false;
    if(s.front() == ':') s.remove_prefix(1);
    if(!s.empty() && s.back() == ':') s.remove_suffix(1);
    if(s.size() < 3) return false;
    for(char c : s)
    {
      if(c != '-') return false;
    }
  }
  return true;
}

bool try_parse_markdown_table(const std::string &markdown,
                              std::size_t line_start,
                              std::size_t line_end,
                              bool has_newline,
                              ParsedMarkdownTable &out)
{
  out = ParsedMarkdownTable{};

  const std::string_view header_line(markdown.data() + line_start, line_end - line_start);
  const std::string_view header_trim = StringUtils::trim(header_line);
  if(header_trim.empty() || StringUtils::starts_with(header_trim, ">")) return false;

  std::vector<std::string> header = split_md_table_cells(header_line);
  if(header.size() < 2) return false;

  const std::size_t sep_start = has_newline ? (line_end + 1) : markdown.size();
  if(sep_start >= markdown.size()) return false;

  std::size_t sep_end = markdown.find('\n', sep_start);
  const bool sep_has_newline = (sep_end != std::string::npos);
  if(!sep_has_newline) sep_end = markdown.size();

  const std::string_view sep_line(markdown.data() + sep_start, sep_end - sep_start);
  if(!is_md_table_separator(sep_line, header.size())) return false;

  std::size_t scan = sep_has_newline ? (sep_end + 1) : markdown.size();
  std::vector<std::vector<std::string>> rows;
  while(scan < markdown.size())
  {
    std::size_t row_end = markdown.find('\n', scan);
    const bool row_has_newline = (row_end != std::string::npos);
    if(!row_has_newline) row_end = markdown.size();

    const std::string_view row_line(markdown.data() + scan, row_end - scan);
    const std::string_view row_trim = StringUtils::trim(row_line);
    if(row_trim.empty() || StringUtils::starts_with(row_trim, ">")) break;

    std::vector<std::string> row_cells = split_md_table_cells(row_line);
    if(row_cells.size() != header.size()) break;

    rows.push_back(std::move(row_cells));
    if(!row_has_newline)
    {
      scan = markdown.size();
      break;
    }
    scan = row_end + 1;
  }

  out.header = std::move(header);
  out.rows = std::move(rows);
  out.block_start = line_start;
  out.block_end = scan;
  out.trailing_newline = (scan > line_start && scan <= markdown.size() && markdown[scan - 1] == '\n');
  return true;
}

std::string normalize_table_cell_value(std::string_view in)
{
  std::string out;
  out.reserve(in.size() + 4);
  for(char c : in)
  {
    if(c == '\r' || c == '\n')
      out.push_back(' ');
    else if(c == '|')
    {
      out.push_back('\\');
      out.push_back('|');
    }
    else
      out.push_back(c);
  }
  return out;
}

std::string build_md_table_line(const std::vector<std::string> &cells)
{
  std::string line = "|";
  for(const std::string &c : cells)
  {
    line += " ";
    line += normalize_table_cell_value(c);
    line += " |";
  }
  return line;
}

std::string build_md_table_separator(std::size_t cols)
{
  std::string line = "|";
  for(std::size_t i = 0; i < cols; ++i)
    line += " --- |";
  return line;
}

std::string build_md_table_markdown(const std::vector<std::string> &header,
                                    const std::vector<std::vector<std::string>> &rows,
                                    bool trailing_newline)
{
  std::string out;
  out += build_md_table_line(header);
  out += '\n';
  out += build_md_table_separator(header.size());
  out += '\n';
  for(const auto &row : rows)
  {
    out += build_md_table_line(row);
    out += '\n';
  }
  if(!trailing_newline && !out.empty() && out.back() == '\n') out.pop_back();
  return out;
}
} // namespace MarkdownTables