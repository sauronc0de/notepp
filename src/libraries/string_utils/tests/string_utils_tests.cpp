#include "string_utils.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_eq(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void test_ltrim()
{
  expect_eq(NoteCore::ltrim(""), "", "ltrim empty");
  expect_eq(NoteCore::ltrim("  hi"), "hi", "ltrim leading spaces");
  expect_eq(NoteCore::ltrim("\t\r hi"), "hi", "ltrim leading tabs/cr");
  expect_eq(NoteCore::ltrim("hi "), "hi ", "ltrim keeps trailing space");
  expect_eq(NoteCore::ltrim("   "), "", "ltrim all spaces");
}

void test_rtrim()
{
  expect_eq(NoteCore::rtrim(""), "", "rtrim empty");
  expect_eq(NoteCore::rtrim("hi  "), "hi", "rtrim trailing spaces");
  expect_eq(NoteCore::rtrim("hi \t\r"), "hi", "rtrim trailing tabs/cr");
  expect_eq(NoteCore::rtrim(" hi"), " hi", "rtrim keeps leading space");
}

void test_trim()
{
  expect_eq(NoteCore::trim("  hi  "), "hi", "trim around");
  expect_eq(NoteCore::trim("\t hi \r"), "hi", "trim mixed whitespace");
  expect_eq(NoteCore::trim("hi"), "hi", "trim no change");
  expect_eq(NoteCore::trim(""), "", "trim empty");
}

void test_starts_with()
{
  expect_true(NoteCore::starts_with("hello world", "hello"), "starts_with positive");
  expect_true(NoteCore::starts_with("hello", "hello"), "starts_with equal");
  expect_true(!NoteCore::starts_with("hello", "world"), "starts_with negative");
  expect_true(!NoteCore::starts_with("hi", "hello"), "starts_with shorter negative");
  expect_true(NoteCore::starts_with("", ""), "starts_with empty both");
  expect_true(!NoteCore::starts_with("", "x"), "starts_with empty haystack");
}

void test_sanitize_note_filename()
{
  expect_eq(NoteCore::sanitize_note_filename("Hello World"), "Hello World", "sanitize plain");
  expect_eq(NoteCore::sanitize_note_filename("a/b\\c:d"), "a_b_c_d", "sanitize slashes/colon");
  expect_eq(NoteCore::sanitize_note_filename("file?*\"<>|"), "file______", "sanitize all bad chars");
  expect_eq(NoteCore::sanitize_note_filename("   "), "note", "sanitize blank becomes default");
  expect_eq(NoteCore::sanitize_note_filename("  ok  "), "ok", "sanitize trim leading/trailing");
  expect_eq(NoteCore::sanitize_note_filename("\n\tname\n"), "name", "sanitize trim newlines/tabs");
}

void test_to_lower_copy()
{
  expect_eq(NoteCore::to_lower_copy("Hello World"), "hello world", "to_lower ascii");
  expect_eq(NoteCore::to_lower_copy("ALREADY"), "already", "to_lower already lower");
  expect_eq(NoteCore::to_lower_copy(""), "", "to_lower empty");
  expect_eq(NoteCore::to_lower_copy("123ABC"), "123abc", "to_lower digits unaffected");
}

void test_clamp01f()
{
  expect_true(NoteCore::clamp01f(-1.0f) == 0.0f, "clamp01f negative");
  expect_true(NoteCore::clamp01f(0.0f) == 0.0f, "clamp01f zero");
  expect_true(NoteCore::clamp01f(0.5f) == 0.5f, "clamp01f mid");
  expect_true(NoteCore::clamp01f(1.0f) == 1.0f, "clamp01f one");
  expect_true(NoteCore::clamp01f(2.0f) == 1.0f, "clamp01f high");
}
} // namespace

int main()
{
  test_ltrim();
  test_rtrim();
  test_trim();
  test_starts_with();
  test_sanitize_note_filename();
  test_to_lower_copy();
  test_clamp01f();
  if(failures != 0)
  {
    std::cerr << failures << " string_utils test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "string_utils tests passed\n";
  return EXIT_SUCCESS;
}
