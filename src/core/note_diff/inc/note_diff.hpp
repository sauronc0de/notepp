#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace notepp::note_diff
{
struct HunkLine
{
  char marker = ' ';
  std::size_t current_line = 0;
  std::size_t baseline_line = 0;
  std::string text;
};

struct PairedLine
{
  std::size_t baseline_line = 0;
  std::size_t current_line = 0;
  bool baseline_present = false;
  bool current_present = false;
  std::string baseline_text;
  std::string current_text;
  bool changed = false;
};

struct Result
{
  bool success = false;
  std::string error;
  std::vector<HunkLine> lines;
  std::vector<PairedLine> paired_lines;
  std::size_t added = 0;
  std::size_t removed = 0;
  std::size_t changed = 0;
  bool same = false;
};

[[nodiscard]] Result compare(std::string_view current, std::string_view baseline,
                             std::size_t max_bytes = 1024U * 1024U,
                             std::size_t max_lines = 100000U);
} // namespace notepp::note_diff
