#include "markdown_widgets.hpp"

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

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void expect_round_trip(std::string_view value_text, std::string_view expected_text, std::string_view label)
{
  std::string pretty_error;
  const std::string pretty = MarkdownWidgets::value_to_pretty_json(value_text, pretty_error);
  expect_true(pretty_error.empty(), std::string("pretty error: ") + std::string(label) + " (" + pretty_error + ")");

  std::string parsed_value_text;
  std::string parse_error;
  const bool ok = MarkdownWidgets::try_parse_strict_json(pretty, parsed_value_text, parse_error);
  expect_true(ok, std::string("parse failed: ") + std::string(label) + " (" + parse_error + ")");
  expect_eq_str(parsed_value_text, expected_text, std::string("round-trip mismatch: ") + std::string(label));
}

void test_strict_keys_always_quoted()
{
  std::string err;
  const std::string out = MarkdownWidgets::value_to_compact_json(R"({"a":1, "name":"x"})", err);
  expect_true(err.empty(), "compact error");
  expect_eq_str(out, R"({"a":1,"name":"x"})", "strict compact always quotes keys");
}

void test_strict_compact_primitives()
{
  std::string err;
  expect_eq_str(MarkdownWidgets::value_to_compact_json("42", err), "42", "compact int");
  expect_eq_str(MarkdownWidgets::value_to_compact_json("3.5", err), "3.5", "compact float");
  expect_eq_str(MarkdownWidgets::value_to_compact_json(R"("hello")", err), "\"hello\"", "compact string");
  expect_eq_str(MarkdownWidgets::value_to_compact_json("true", err), "true", "compact true");
  expect_eq_str(MarkdownWidgets::value_to_compact_json("false", err), "false", "compact false");
  expect_eq_str(MarkdownWidgets::value_to_compact_json("null", err), "null", "compact null");
  expect_true(err.empty(), "compact primitives error");
}

void test_strict_pretty_indentation()
{
  std::string err;
  const std::string pretty = MarkdownWidgets::value_to_pretty_json(R"({"a":[1,2],"b":{}})", err);
  expect_true(err.empty(), "pretty error");
  const std::string expected =
      "{\n"
      "  \"a\": [\n"
      "    1,\n"
      "    2\n"
      "  ],\n"
      "  \"b\": {}\n"
      "}";
  expect_eq_str(pretty, expected, "pretty indentation");
}

void test_strict_escape_sequences()
{
  std::string err;
  const std::string out = MarkdownWidgets::value_to_compact_json(R"("line1\nline2\t\"end\"")", err);
  expect_true(err.empty(), "escape error");
  expect_eq_str(out, "\"line1\\nline2\\t\\\"end\\\"\"", "escape sequences");
}

void test_strict_pretty_empty_containers()
{
  std::string err;
  expect_eq_str(MarkdownWidgets::value_to_pretty_json("[]", err), "[]", "pretty empty array");
  expect_eq_str(MarkdownWidgets::value_to_pretty_json("{}", err), "{}", "pretty empty object");
  expect_true(err.empty(), "pretty empty error");
}

void test_parser_accepts_strict_json()
{
  std::string value;
  std::string err;
  expect_true(MarkdownWidgets::try_parse_strict_json(R"({"a":1})", value, err), "parse object");
  expect_true(err.empty(), "parse object error");
  expect_eq_str(value, "{a:1}", "parse object round-trip text");

  expect_true(MarkdownWidgets::try_parse_strict_json(R"([1,2,3])", value, err), "parse array");
  expect_eq_str(value, "[1, 2, 3]", "parse array round-trip text");

  expect_true(MarkdownWidgets::try_parse_strict_json(R"("hello")", value, err), "parse string");
  expect_eq_str(value, R"("hello")", "parse string round-trip text");

  expect_true(MarkdownWidgets::try_parse_strict_json("null", value, err), "parse null");
  expect_eq_str(value, "null", "parse null round-trip text");

  expect_true(MarkdownWidgets::try_parse_strict_json("42", value, err), "parse int");
  expect_eq_str(value, "42", "parse int round-trip text");

  expect_true(MarkdownWidgets::try_parse_strict_json("-3.5", value, err), "parse float");
  expect_eq_str(value, "-3.5", "parse float round-trip text");
}

void test_parser_rejects_relaxed_syntax()
{
  std::string value;
  std::string err;
  expect_true(!MarkdownWidgets::try_parse_strict_json("{a:1}", value, err), "rejects unquoted key");
  expect_true(!err.empty(), "rejects unquoted key produces error");
  err.clear();

  expect_true(!MarkdownWidgets::try_parse_strict_json("'single'", value, err), "rejects single quotes");
  err.clear();

  expect_true(!MarkdownWidgets::try_parse_strict_json("[1, 2,]", value, err), "rejects trailing comma");
  err.clear();
}

void test_parser_error_position()
{
  std::string value;
  std::string err;
  expect_true(!MarkdownWidgets::try_parse_strict_json(R"({"a": 1, "b": @})", value, err), "rejects bad token");
  expect_true(err.find("position") != std::string::npos, "error contains position");
}

void test_parser_unicode_escape()
{
  std::string value;
  std::string err;
  expect_true(MarkdownWidgets::try_parse_strict_json(R"("é")", value, err), "parse utf-8 string");
  expect_true(MarkdownWidgets::try_parse_strict_json(R"("\u00e9")", value, err), "parse unicode escape");
  expect_eq_str(value, R"("é")", "unicode escape round-trip");
}

void test_round_trip_complex()
{
  expect_round_trip(
      R"({"name":"Ana","age":30,"tags":["a","b"],"meta":{"active":true,"score":4.5}})",
      R"({name:"Ana", age:30, tags:["a", "b"], meta:{active:true, score:4.5}})",
      "complex object");
  expect_round_trip(
      R"([1,2,3,{"x":true}])",
      R"([1, 2, 3, {x:true}])",
      "mixed array");
  expect_round_trip(
      R"({"nested":{"deep":{"value":"ok"}}})",
      R"({nested:{deep:{value:"ok"}}})",
      "deeply nested");
  expect_round_trip(
      R"({"empty_obj":{},"empty_arr":[],"zero":0,"false":false})",
      R"({empty_obj:{}, empty_arr:[], zero:0, false:false})",
      "edge values");
}
} // namespace

int main()
{
  test_strict_keys_always_quoted();
  test_strict_compact_primitives();
  test_strict_pretty_indentation();
  test_strict_escape_sequences();
  test_strict_pretty_empty_containers();
  test_parser_accepts_strict_json();
  test_parser_rejects_relaxed_syntax();
  test_parser_error_position();
  test_parser_unicode_escape();
  test_round_trip_complex();
  if(failures != 0)
  {
    std::cerr << failures << " json_widgets test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "json_widgets tests passed\n";
  return EXIT_SUCCESS;
}
