#include "note_diff.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace notepp::note_diff
{
namespace
{
struct Line
{
  std::string text;
};

std::vector<Line> split_lines(std::string_view value)
{
  std::vector<Line> result;
  std::size_t start = 0;
  while(start < value.size())
  {
    const std::size_t end = value.find('\n', start);
    const std::size_t limit = end == std::string_view::npos ? value.size() : end;
    std::string line(value.substr(start, limit - start));
    if(end != std::string_view::npos && !line.empty() && line.back() == '\r') line.pop_back();
    result.push_back({std::move(line)});
    if(end == std::string_view::npos) break;
    start = end + 1U;
    // A terminal newline terminates the final line; it does not introduce an
    // additional empty line. This keeps newline-style differences out of the
    // line diff and its counts.
  }
  return result;
}

struct Edit
{
  char marker = ' ';
  std::size_t current = 0;
  std::size_t baseline = 0;
  std::string text;
};
} // namespace

Result compare(std::string_view current, std::string_view baseline, std::size_t max_bytes,
               std::size_t max_lines)
{
  Result result;
  if(current.size() > max_bytes || baseline.size() > max_bytes)
  {
    result.error = "Note is too large to compare";
    return result;
  }
  const std::vector<Line> current_lines = split_lines(current);
  const std::vector<Line> baseline_lines = split_lines(baseline);
  if(current_lines.size() > max_lines || baseline_lines.size() > max_lines)
  {
    result.error = "Note has too many lines to compare";
    return result;
  }
  const std::size_t rows = current_lines.size();
  const std::size_t columns = baseline_lines.size();
  if(columns != 0U && rows > (std::numeric_limits<std::size_t>::max() / columns))
  {
    result.error = "Note is too large to compare";
    return result;
  }
  constexpr std::size_t max_cells = 4U * 1000U * 1000U;
  if(rows * columns > max_cells)
  {
    result.error = "Note is too large to compare";
    return result;
  }

  std::vector<std::size_t> lcs((rows + 1U) * (columns + 1U));
  for(std::size_t row = rows; row-- > 0U;)
    for(std::size_t column = columns; column-- > 0U;)
      lcs[row * (columns + 1U) + column] = current_lines[row].text == baseline_lines[column].text
                                               ? lcs[(row + 1U) * (columns + 1U) + column + 1U] + 1U
                                               : std::max(lcs[(row + 1U) * (columns + 1U) + column],
                                                          lcs[row * (columns + 1U) + column + 1U]);

  std::vector<Edit> edits;
  std::size_t row = 0;
  std::size_t column = 0;
  while(row < rows || column < columns)
  {
    if(row < rows && column < columns && current_lines[row].text == baseline_lines[column].text)
    {
      edits.push_back({' ', row + 1U, column + 1U, current_lines[row].text});
      ++row;
      ++column;
    }
    else if(column == columns || (row < rows && lcs[(row + 1U) * (columns + 1U) + column] >=
                                                    lcs[row * (columns + 1U) + column + 1U]))
    {
      edits.push_back({'+', row + 1U, column + 1U, current_lines[row].text});
      ++row;
    }
    else
    {
      edits.push_back({'-', row + 1U, column + 1U, baseline_lines[column].text});
      ++column;
    }
  }

  result.success = true;
  result.same = current == baseline;
  if(!result.same && current_lines.size() == baseline_lines.size())
  {
    result.same = true;
    for(std::size_t index = 0; index < current_lines.size(); ++index)
      if(current_lines[index].text != baseline_lines[index].text)
      {
        result.same = false;
        break;
      }
  }
  for(const Edit &edit : edits)
  {
    result.lines.push_back({edit.marker, edit.current, edit.baseline, edit.text});
    if(edit.marker == '+') ++result.added;
    if(edit.marker == '-') ++result.removed;
  }
  std::size_t index = 0;
  while(index < edits.size())
  {
    if(edits[index].marker == ' ')
    {
      const Edit &edit = edits[index++];
      result.paired_lines.push_back({edit.baseline, edit.current, true, true, edit.text, edit.text, false});
      continue;
    }

    std::vector<const Edit *> removed;
    std::vector<const Edit *> added;
    while(index < edits.size() && edits[index].marker != ' ')
    {
      if(edits[index].marker == '-')
        removed.push_back(&edits[index]);
      else if(edits[index].marker == '+')
        added.push_back(&edits[index]);
      ++index;
    }
    result.changed += std::min(removed.size(), added.size());
    const std::size_t pair_count = std::max(removed.size(), added.size());
    for(std::size_t pair = 0; pair < pair_count; ++pair)
    {
      const Edit *old_line = pair < removed.size() ? removed[pair] : nullptr;
      const Edit *new_line = pair < added.size() ? added[pair] : nullptr;
      result.paired_lines.push_back({old_line != nullptr ? old_line->baseline : 0U,
                                     new_line != nullptr ? new_line->current : 0U,
                                     old_line != nullptr,
                                     new_line != nullptr,
                                     old_line != nullptr ? old_line->text : std::string{},
                                     new_line != nullptr ? new_line->text : std::string{},
                                     old_line != nullptr && new_line != nullptr});
    }
  }
  return result;
}
} // namespace notepp::note_diff
