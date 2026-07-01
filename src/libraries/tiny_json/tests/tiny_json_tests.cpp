#include "tiny_json.hpp"

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

void test_json_escape()
{
  expect_eq_str(TinyJson::json_escape(""), "", "escape empty");
  expect_eq_str(TinyJson::json_escape("hello"), "hello", "escape plain");
  expect_eq_str(TinyJson::json_escape("a\"b"), "a\\\"b", "escape quote");
  expect_eq_str(TinyJson::json_escape("a\\b"), "a\\\\b", "escape backslash");
  expect_eq_str(TinyJson::json_escape("a\nb"), "a\\nb", "escape newline");
  expect_eq_str(TinyJson::json_escape("a\tb"), "a\\tb", "escape tab");
  expect_eq_str(TinyJson::json_escape("a\rb"), "a\\rb", "escape carriage");
}

void test_json_unescape()
{
  expect_eq_str(TinyJson::json_unescape(""), "", "unescape empty");
  expect_eq_str(TinyJson::json_unescape("hello"), "hello", "unescape plain");
  expect_eq_str(TinyJson::json_unescape("a\\\"b"), "a\"b", "unescape quote");
  expect_eq_str(TinyJson::json_unescape("a\\\\b"), "a\\b", "unescape backslash");
  expect_eq_str(TinyJson::json_unescape("a\\nb"), "a\nb", "unescape newline");
  expect_eq_str(TinyJson::json_unescape("a\\tb"), "a\tb", "unescape tab");
}

void test_find_matching()
{
  expect_true(TinyJson::find_matching("{}", 0, '{', '}') == 1, "find_matching simple");
  expect_true(TinyJson::find_matching("{a{b}c}", 0, '{', '}') == 6, "find_matching nested");
  expect_true(TinyJson::find_matching("[1,2,3]", 0, '[', ']') == 6, "find_matching array");
  expect_true(TinyJson::find_matching("\"{}\"", 0, '{', '}') == std::string::npos, "find_matching ignores string braces");
  expect_true(TinyJson::find_matching("{\"a\":\"x{1}\"}", 0, '{', '}') == 11, "find_matching nested string");
  expect_true(TinyJson::find_matching("abc", 0, '{', '}') == std::string::npos, "find_matching no open");
}

void test_json_find_string()
{
  expect_eq_str(TinyJson::json_find_string(R"({"name":"Alice"})", "name"), "Alice", "find_string simple");
  expect_eq_str(TinyJson::json_find_string(R"({"k":""})", "k"), "", "find_string empty value");
  expect_eq_str(TinyJson::json_find_string(R"({"msg":"a\nb"})", "msg"), "a\nb", "find_string escaped");
  expect_eq_str(TinyJson::json_find_string(R"({"other":1})", "name"), "", "find_string missing key");
}

void test_json_find_int()
{
  expect_true(TinyJson::json_find_int(R"({"n":42})", "n", -1) == 42, "find_int simple");
  expect_true(TinyJson::json_find_int(R"({"n":-7})", "n", 0) == -7, "find_int negative");
  expect_true(TinyJson::json_find_int(R"({})", "n", 9) == 9, "find_int missing default");
  expect_true(TinyJson::json_find_int(R"({"n":"42"})", "n", 7) == 7, "find_int non-numeric default");
}

void test_json_find_float()
{
  expect_true(TinyJson::json_find_float(R"({"f":3.5})", "f", 0.0f) == 3.5f, "find_float simple");
  expect_true(TinyJson::json_find_float(R"({"f":-2.25})", "f", 0.0f) == -2.25f, "find_float negative");
  expect_true(TinyJson::json_find_float(R"({})", "f", 1.5f) == 1.5f, "find_float missing default");
}

void test_json_find_bool()
{
  expect_true(TinyJson::json_find_bool(R"({"b":true})", "b", false) == true, "find_bool true");
  expect_true(TinyJson::json_find_bool(R"({"b":false})", "b", true) == false, "find_bool false");
  expect_true(TinyJson::json_find_bool(R"({})", "b", true) == true, "find_bool missing default true");
  expect_true(TinyJson::json_find_bool(R"({})", "b", false) == false, "find_bool missing default false");
  expect_true(TinyJson::json_find_bool(R"({"b":"yes"})", "b", false) == false, "find_bool non-bool default");
}

void test_json_array_objects()
{
  std::vector<std::string_view> parts = TinyJson::json_array_objects(R"([{"a":1},{"b":2}])");
  expect_true(parts.size() == 2, "array_objects count");
  expect_eq_str(parts.front(), R"({"a":1})", "array_objects first");
  expect_eq_str(parts.back(), R"({"b":2})", "array_objects second");

  std::vector<std::string_view> empty = TinyJson::json_array_objects("[]");
  expect_true(empty.empty(), "array_objects empty");

  std::vector<std::string_view> nested = TinyJson::json_array_objects(R"([{"a":{"x":1}},{"b":2}])");
  expect_true(nested.size() == 2, "array_objects nested count");
  expect_eq_str(nested[0], R"({"a":{"x":1}})", "array_objects nested first");
}
} // namespace

int main()
{
  test_json_escape();
  test_json_unescape();
  test_find_matching();
  test_json_find_string();
  test_json_find_int();
  test_json_find_float();
  test_json_find_bool();
  test_json_array_objects();
  if(failures != 0)
  {
    std::cerr << failures << " tiny_json test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "tiny_json tests passed\n";
  return EXIT_SUCCESS;
}
