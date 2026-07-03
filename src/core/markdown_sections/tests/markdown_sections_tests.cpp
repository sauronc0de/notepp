#include "markdown_sections.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void expect_eq_int(long long a, long long b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got " << a << ", expected " << b << ")\n";
}

void test_parse_heading_line()
{
  int level = -1;
  std::string_view title;

  expect_true(parse_heading_line("# Hello", level, title), "h1 detected");
  expect_eq_int(level, 1, "h1 level");
  expect_eq_str(title, "Hello", "h1 title");

  expect_true(parse_heading_line("### Subsection", level, title), "h3 detected");
  expect_eq_int(level, 3, "h3 level");
  expect_eq_str(title, "Subsection", "h3 title");

  expect_true(parse_heading_line("  ##   Trim me  ", level, title), "h2 with leading/trailing space");
  expect_eq_int(level, 2, "h2 level trimmed");
  expect_eq_str(title, "Trim me", "h2 title trimmed");

  expect_true(!parse_heading_line("####### Too many", level, title), "more than 6 hashes rejected");
  expect_true(!parse_heading_line("NoHash", level, title), "line without # rejected");
  expect_true(!parse_heading_line("#NoSpace", level, title), "missing space after # rejected");
}

void test_parse_sections_simple()
{
  const std::string md =
      "# Title\n"
      "intro line 1\n"
      "intro line 2\n";

  MdSection root = parse_sections(md);
  expect_eq_int(static_cast<long long>(root.kids.size()), 1, "one top section");
  expect_eq_int(root.kids[0].level, 1, "top section is level 1");
  expect_eq_str(root.kids[0].title, "Title", "top section title");
  expect_eq_str(root.kids[0].body, "intro line 1\nintro line 2\n", "body before any subheading");
}

void test_parse_sections_nested()
{
  const std::string md =
      "# A\n"
      "a body\n"
      "## A.1\n"
      "a.1 body\n"
      "## A.2\n"
      "a.2 body\n"
      "# B\n"
      "b body\n";

  MdSection root = parse_sections(md);
  expect_eq_int(static_cast<long long>(root.kids.size()), 2, "two top-level sections");
  expect_eq_str(root.kids[0].title, "A", "first top title");
  expect_eq_str(root.kids[1].title, "B", "second top title");
  expect_eq_str(root.kids[1].body, "b body\n", "second top body");

  expect_eq_int(static_cast<long long>(root.kids[0].kids.size()), 2, "A has two children");
  expect_eq_str(root.kids[0].kids[0].title, "A.1", "first child title");
  expect_eq_str(root.kids[0].kids[0].body, "a.1 body\n", "first child body");
  expect_eq_str(root.kids[0].kids[1].title, "A.2", "second child title");
}

void test_parse_sections_empty()
{
  MdSection root = parse_sections("");
  expect_true(root.kids.empty(), "no sections from empty input");
  expect_eq_str(root.body, "", "empty body");
}

void test_parse_sections_no_headings()
{
  const std::string md = "just text\nmore text\n";
  MdSection root = parse_sections(md);
  expect_true(root.kids.empty(), "no headings => no children");
  expect_eq_str(root.body, "just text\nmore text\n", "everything ends up in root body");
}
} // namespace

int main()
{
  test_parse_heading_line();
  test_parse_sections_simple();
  test_parse_sections_nested();
  test_parse_sections_empty();
  test_parse_sections_no_headings();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_sections test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_sections tests passed\n";
  return EXIT_SUCCESS;
}
