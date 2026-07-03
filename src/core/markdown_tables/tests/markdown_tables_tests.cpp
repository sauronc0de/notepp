#include "markdown_tables.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace mt = MarkdownTables;

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_false(bool cond, std::string_view msg)
{
  expect_true(!cond, msg);
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void expect_eq_size(std::size_t a, std::size_t b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got " << a << ", expected " << b << ")\n";
}

void test_split_cells_basic()
{
  std::vector<std::string> cells = mt::split_md_table_cells("| a | b | c |");
  expect_eq_size(cells.size(), 3, "three cells");
  expect_eq_str(cells[0], "a", "cell 0");
  expect_eq_str(cells[2], "c", "cell 2");
}

void test_split_cells_no_pipes()
{
  std::vector<std::string> cells = mt::split_md_table_cells("no pipes here");
  expect_eq_size(cells.size(), 0, "no pipes returns empty");
}

void test_split_cells_trims_whitespace()
{
  std::vector<std::string> cells = mt::split_md_table_cells("|  hello  |  world |");
  expect_eq_size(cells.size(), 2, "two cells");
  expect_eq_str(cells[0], "hello", "trimmed left");
  expect_eq_str(cells[1], "world", "trimmed right");
}

void test_split_cells_empty_cells()
{
  std::vector<std::string> cells = mt::split_md_table_cells("| | |");
  expect_eq_size(cells.size(), 2, "empty cells become 2 entries (one empty)");
  expect_eq_str(cells[0], "", "first cell empty");
}

void test_separator_valid()
{
  expect_true(mt::is_md_table_separator("| --- | --- |", 2), "basic separator");
  expect_true(mt::is_md_table_separator("| :--- | :---: | ---: |", 3), "alignment colons");
  expect_true(mt::is_md_table_separator("| --- |", 1), "single col with pipes");
}

void test_separator_invalid()
{
  expect_false(mt::is_md_table_separator("| abc | def |", 2), "letters not allowed");
  expect_false(mt::is_md_table_separator("| - | -- |", 2), "less than 3 dashes");
  expect_false(mt::is_md_table_separator("| --- | --- |", 3), "wrong column count");
}

void test_parse_valid_table()
{
  const std::string md =
      "| A | B |\n"
      "| --- | --- |\n"
      "| 1 | 2 |\n"
      "| 3 | 4 |\n";

  mt::ParsedMarkdownTable out;
  expect_true(mt::try_parse_markdown_table(md, 0, 9, true, out), "valid table parses");
  expect_eq_size(out.header.size(), 2, "header columns");
  expect_eq_str(out.header[0], "A", "header A");
  expect_eq_size(out.rows.size(), 2, "two data rows");
  expect_eq_str(out.rows[1][1], "4", "row 1 col 1");
}

void test_parse_no_separator_rejects()
{
  const std::string md =
      "| A | B |\n"
      "| 1 | 2 |\n";

  mt::ParsedMarkdownTable out;
  expect_false(mt::try_parse_markdown_table(md, 0, 9, true, out), "missing separator rejected");
}

void test_parse_inconsistent_columns_truncates()
{
  const std::string md =
      "| A | B |\n"
      "| --- | --- |\n"
      "| 1 | 2 | 3 |\n";

  mt::ParsedMarkdownTable out;
  expect_true(mt::try_parse_markdown_table(md, 0, 9, true, out), "parses up to first bad row");
  expect_eq_size(out.header.size(), 2, "header columns");
  expect_eq_size(out.rows.size(), 0, "no rows accepted after column mismatch");
}

void test_parse_empty_header_rejects()
{
  const std::string md =
      "   \n"
      "| --- |\n";

  mt::ParsedMarkdownTable out;
  expect_false(mt::try_parse_markdown_table(md, 0, 3, true, out), "empty header rejected");
}

void test_parse_escaped_pipes_treated_as_separators()
{
  const std::string md =
      "| A \\| B | C |\n"
      "| --- | --- |\n"
      "| x | y |\n";

  // The basic splitter does NOT understand escaped pipes; they are split
  // exactly like any other pipe. The header is split into 3 cells but the
  // separator only has 2 columns so the block is rejected.
  mt::ParsedMarkdownTable out;
  expect_false(mt::try_parse_markdown_table(md, 0, 14, true, out), "header/separator column mismatch");
}

void test_normalize_cell_value_escapes_pipe()
{
  expect_eq_str(mt::normalize_table_cell_value("a|b"), "a\\|b", "pipe escaped");
}

void test_normalize_cell_value_replaces_newlines()
{
  expect_eq_str(mt::normalize_table_cell_value("a\nb\rc"), "a b c", "newlines -> spaces");
}

void test_build_table_line()
{
  std::vector<std::string> cells = {"A", "B"};
  expect_eq_str(mt::build_md_table_line(cells), "| A | B |", "table line");
}

void test_build_table_separator()
{
  expect_eq_str(mt::build_md_table_separator(3), "| --- | --- | --- |", "3-column separator");
}

void test_build_table_markdown()
{
  std::vector<std::string> header = {"H1", "H2"};
  std::vector<std::vector<std::string>> rows = {{"a", "b"}, {"c", "d"}};
  std::string md = mt::build_md_table_markdown(header, rows, true);
  std::string expected =
      "| H1 | H2 |\n"
      "| --- | --- |\n"
      "| a | b |\n"
      "| c | d |\n";
  expect_eq_str(md, expected, "full table markdown");
}

void test_build_table_markdown_no_trailing_newline()
{
  std::vector<std::string> header = {"H1"};
  std::vector<std::vector<std::string>> rows = {{"a"}};
  std::string md = mt::build_md_table_markdown(header, rows, false);
  expect_eq_str(md, "| H1 |\n| --- |\n| a |", "no trailing newline");
}
} // namespace

int main()
{
  test_split_cells_basic();
  test_split_cells_no_pipes();
  test_split_cells_trims_whitespace();
  test_split_cells_empty_cells();
  test_separator_valid();
  test_separator_invalid();
  test_parse_valid_table();
  test_parse_no_separator_rejects();
  test_parse_inconsistent_columns_truncates();
  test_parse_empty_header_rejects();
  test_parse_escaped_pipes_treated_as_separators();
  test_normalize_cell_value_escapes_pipe();
  test_normalize_cell_value_replaces_newlines();
  test_build_table_line();
  test_build_table_separator();
  test_build_table_markdown();
  test_build_table_markdown_no_trailing_newline();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_tables test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_tables tests passed\n";
  return EXIT_SUCCESS;
}