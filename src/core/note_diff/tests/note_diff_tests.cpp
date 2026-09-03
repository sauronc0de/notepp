#include "note_diff.hpp"

#include <iostream>
#include <string_view>

namespace
{
int failures = 0;
void expect(bool condition, std::string_view message)
{
  if(!condition)
  {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
} // namespace

int main()
{
  using notepp::note_diff::compare;
  const auto insertion = compare("one\ntwo\nthree\n", "one\nthree\n");
  expect(insertion.success && insertion.added == 1U && insertion.removed == 0U,
         "insertion counts are deterministic");
  expect(insertion.paired_lines.size() == 3U && !insertion.paired_lines[1].baseline_present &&
             insertion.paired_lines[1].current_present && insertion.paired_lines[1].current_text == "two",
         "insertions pair an empty previous row with the new row");
  const auto deletion = compare("one\nthree\n", "one\ntwo\nthree\n");
  expect(deletion.paired_lines.size() == 3U && deletion.paired_lines[1].baseline_present &&
             !deletion.paired_lines[1].current_present && deletion.paired_lines[1].baseline_text == "two",
         "deletions pair the old row with an empty new row");
  const auto replacement = compare("one\nnew\n", "one\nold\n");
  expect(replacement.success && replacement.added == 1U && replacement.removed == 1U &&
             replacement.changed == 1U,
         "replacement counts added, removed, and changed lines");
  expect(replacement.paired_lines.size() == 2U && replacement.paired_lines[1].changed &&
             replacement.paired_lines[1].baseline_text == "old" &&
             replacement.paired_lines[1].current_text == "new",
         "replacements pair previous and new rows");
  expect(compare("one\r\ntwo\r\n", "one\ntwo\n").same, "CRLF and LF compare equally");
  expect(!compare("one\rtwo", "one\ntwo").same, "lone CR is preserved as content");
  expect(!compare("x", "", 0U).success, "byte limit rejects oversized input");
  expect(!compare("x\ny", "", 1024U, 1U).success, "line limit rejects oversized input");
  expect(compare("", "").same, "empty documents compare equally");
  const auto final_newline = compare("one\n", "one");
  expect(final_newline.success && final_newline.same && final_newline.added == 0U &&
             final_newline.removed == 0U && final_newline.changed == 0U,
         "final newline-only differences do not count as line changes");
  return failures == 0 ? 0 : 1;
}
