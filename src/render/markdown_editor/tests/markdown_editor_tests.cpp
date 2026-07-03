#include "markdown_editor.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace me = MarkdownEditor;

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

void test_insert_checklist_at_start()
{
  std::string text;
  me::MdFormatState fmt;
  fmt.cursor_pos = 0;
  me::insert_checklist_item_at_cursor(text, fmt);
  expect_eq_str(text, "- [ ] ", "checklist inserted at empty cursor");
  expect_eq_int(fmt.cursor_pos, 6, "cursor after checklist");
}

void test_insert_checklist_after_text()
{
  std::string text = "abc";
  me::MdFormatState fmt;
  fmt.cursor_pos = 3;
  me::insert_checklist_item_at_cursor(text, fmt);
  expect_eq_str(text, "abc\n- [ ] ", "newline added before checklist");
  expect_eq_int(fmt.cursor_pos, 10, "cursor advanced past newline+checklist");
}

void test_insert_checklist_after_newline()
{
  std::string text = "abc\n";
  me::MdFormatState fmt;
  fmt.cursor_pos = 4;
  me::insert_checklist_item_at_cursor(text, fmt);
  expect_eq_str(text, "abc\n- [ ] ", "no extra newline after existing newline");
  expect_eq_int(fmt.cursor_pos, 10, "cursor at end");
}

void test_insert_table_default_size()
{
  std::string text;
  me::MdFormatState fmt;
  fmt.cursor_pos = 0;
  me::insert_markdown_table_at_cursor(text, fmt);
  expect_true(text.find("| Header 1 |") != std::string::npos, "header row present");
  expect_true(text.find("| --- |") != std::string::npos, "separator row present");
  expect_true(text.find("| Cell 1,1 |") != std::string::npos, "data row present");
}

void test_apply_wrap_string_basic()
{
  std::string s = "hello world";
  int a = 0;
  int b = 5;
  me::apply_wrap_string(s, a, b, "**", "**");
  expect_eq_str(s, "**hello** world", "wrapped with bold markers");
  expect_eq_int(a, 2, "selection start after left marker");
  expect_eq_int(b, 7, "selection end after left marker");
}

void test_apply_wrap_string_reversed_selection()
{
  std::string s = "abcdef";
  int a = 4;
  int b = 1;
  me::apply_wrap_string(s, a, b, "<", ">");
  expect_eq_str(s, "a<bcd>ef", "wrap normalizes reversed selection");
  expect_eq_int(a, 2, "sel start normalized");
  expect_eq_int(b, 5, "sel end normalized");
}

void test_apply_wrap_string_empty_selection()
{
  std::string s = "hello";
  int a = 2;
  int b = 2;
  me::apply_wrap_string(s, a, b, "**", "**");
  expect_eq_str(s, "hello", "empty selection no-op");
}

void test_apply_wrap_string_clamps_oversize()
{
  std::string s = "hi";
  int a = 0;
  int b = 100;
  me::apply_wrap_string(s, a, b, "(", ")");
  expect_eq_str(s, "(hi)", "clamped over-size selection");
}

void test_apply_color_wrap_string()
{
  std::string s = "hello";
  int a = 0;
  int b = 5;
  me::apply_color_wrap_string(s, a, b, "#FF8800");
  expect_eq_str(s, "[color=#FF8800]hello[/color]", "color wrap inserted");
}

void test_apply_color_wrap_string_strips_trailing_newlines()
{
  std::string s = "hello\n\n";
  int a = 0;
  int b = 7;
  me::apply_color_wrap_string(s, a, b, "#FF8800");
  expect_eq_str(s, "[color=#FF8800]hello[/color]\n\n", "trailing newlines stripped from selection");
}

void test_apply_note_quote_single_line()
{
  std::string s = "hello";
  int a = 0;
  int b = 5;
  me::apply_note_quote(s, a, b);
  expect_eq_str(s, "> hello", "single line quoted");
}

void test_apply_note_quote_multiline()
{
  std::string s = "a\nb\nc";
  int a = 0;
  int b = 5;
  me::apply_note_quote(s, a, b);
  expect_eq_str(s, "> a\n> b\n> c", "all lines quoted");
}

void test_apply_note_quote_partial_selection()
{
  std::string s = "alpha\nbeta\ngamma";
  int a = 3;  // start mid-line
  int b = 11; // end after "beta"
  me::apply_note_quote(s, a, b);
  expect_eq_str(s, "> alpha\n> beta\n> gamma", "partial selection expands to whole lines");
}

void test_apply_note_quote_empty_selection()
{
  std::string s = "hello";
  int a = 2;
  int b = 2;
  me::apply_note_quote(s, a, b);
  expect_eq_str(s, "hello", "empty selection no-op");
}

void test_rgba_to_hex()
{
  ImVec4 c(1.0f, 0.0f, 0.0f, 1.0f);
  expect_eq_str(me::rgba_to_hex(c), "#FF0000", "red");
  ImVec4 g(0.0f, 1.0f, 0.0f, 1.0f);
  expect_eq_str(me::rgba_to_hex(g), "#00FF00", "green");
  ImVec4 mid(0.5f, 0.5f, 0.5f, 1.0f);
  // clamp01f(0.5) * 255 + 0.5 = 128 (int truncated) so 0x80
  expect_eq_str(me::rgba_to_hex(mid), "#808080", "mid gray");
}

void test_rgba_to_hex_clamps()
{
  ImVec4 over(1.5f, -0.5f, 0.5f, 1.0f);
  expect_eq_str(me::rgba_to_hex(over), "#FF0080", "clamps out of range");
}

void test_line_bounds_middle()
{
  std::string t = "abc\ndef\nghi";
  auto b = me::line_bounds_from_cursor(t, 5);
  expect_eq_int(b.first, 4, "line 2 start");
  expect_eq_int(b.second, 7, "line 2 end");
}

void test_line_bounds_clamps_oversize()
{
  std::string t = "abc";
  auto b = me::line_bounds_from_cursor(t, 100);
  expect_eq_int(b.first, 0, "clamped start");
  expect_eq_int(b.second, 3, "clamped end");
}

void test_line_bounds_negative()
{
  std::string t = "abc";
  auto b = me::line_bounds_from_cursor(t, -5);
  expect_eq_int(b.first, 0, "negative clamped to 0");
  expect_eq_int(b.second, 3, "negative end equals size");
}

void test_word_bounds_basic()
{
  std::string t = "hello world";
  auto b = me::word_bounds_from_double_click(t, 2, 0, 5);
  expect_eq_int(b.first, 0, "first word start");
  expect_eq_int(b.second, 5, "first word end");
}

void test_word_bounds_no_word_char()
{
  std::string t = "   spaces   ";
  auto b = me::word_bounds_from_double_click(t, 2, 0, 11);
  expect_eq_int(b.first, 3, "no word char advances past spaces");
  expect_eq_int(b.second, 9, "no word char stops at end of spaces");
}

void test_word_bounds_empty_selection()
{
  std::string t = "hello world";
  auto b = me::word_bounds_from_double_click(t, 2, 2, 2);
  expect_eq_int(b.first, 2, "empty selection returns cursor");
  expect_eq_int(b.second, 2, "empty selection returns cursor");
}

void test_should_push_undo_no_change()
{
  std::string a = "abc";
  me::MdFormatState st;
  st.cursor_pos = 3;
  expect_true(!me::should_push_word_granular_undo(a, a, st), "no change returns false");
}

void test_should_push_undo_single_insert_word_char()
{
  std::string before = "ab";
  std::string after = "abc";
  me::MdFormatState st;
  st.cursor_pos = 3;
  expect_true(me::should_push_word_granular_undo(before, after, st), "first word char starts a group");
  // second insert: cursor now == last_edit + 1, contiguous -> continues group
  std::string before2 = "abc";
  std::string after2 = "abcd";
  st.cursor_pos = 4;
  expect_true(!me::should_push_word_granular_undo(before2, after2, st), "second word char continues group");
}

void test_should_push_undo_single_insert_space()
{
  std::string before = "abc";
  std::string after = "abc ";
  me::MdFormatState st;
  st.cursor_pos = 4;
  expect_true(!me::should_push_word_granular_undo(before, after, st), "space insertion is non-granular");
}

void test_should_push_undo_multi_char_change()
{
  std::string before = "abc";
  std::string after = "axyz";
  me::MdFormatState st;
  st.cursor_pos = 4;
  expect_true(me::should_push_word_granular_undo(before, after, st), "multi-char change always pushes");
}

void test_normalize_input_text_buffer_truncates_at_nul()
{
  std::string s;
  s.reserve(16);
  s.append("hello");
  s.push_back('\0');
  s.append("world");
  me::normalize_input_text_buffer(s);
  expect_eq_str(s, "hello", "truncates at first NUL");
}

void test_normalize_input_text_buffer_no_change()
{
  std::string s = "hello";
  me::normalize_input_text_buffer(s);
  expect_eq_str(s, "hello", "no-op when no NUL");
}
} // namespace

int main()
{
  test_insert_checklist_at_start();
  test_insert_checklist_after_text();
  test_insert_checklist_after_newline();
  test_insert_table_default_size();
  test_apply_wrap_string_basic();
  test_apply_wrap_string_reversed_selection();
  test_apply_wrap_string_empty_selection();
  test_apply_wrap_string_clamps_oversize();
  test_apply_color_wrap_string();
  test_apply_color_wrap_string_strips_trailing_newlines();
  test_apply_note_quote_single_line();
  test_apply_note_quote_multiline();
  test_apply_note_quote_partial_selection();
  test_apply_note_quote_empty_selection();
  test_rgba_to_hex();
  test_rgba_to_hex_clamps();
  test_line_bounds_middle();
  test_line_bounds_clamps_oversize();
  test_line_bounds_negative();
  test_word_bounds_basic();
  test_word_bounds_no_word_char();
  test_word_bounds_empty_selection();
  test_should_push_undo_no_change();
  test_should_push_undo_single_insert_word_char();
  test_should_push_undo_single_insert_space();
  test_should_push_undo_multi_char_change();
  test_normalize_input_text_buffer_truncates_at_nul();
  test_normalize_input_text_buffer_no_change();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_editor test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_editor tests passed\n";
  return EXIT_SUCCESS;
}