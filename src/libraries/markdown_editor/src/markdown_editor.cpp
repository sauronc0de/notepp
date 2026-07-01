#include "markdown_editor.hpp"

#include "string_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace MarkdownEditor
{
namespace
{
bool is_word_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::pair<int, int> expand_word_bounds(const std::string &text, int pos)
{
  int n = static_cast<int>(text.size());
  pos = std::max(0, std::min(pos, n));
  int start = pos;
  int end = pos;
  if(start < n && is_word_char(text[static_cast<size_t>(start)]))
  {
    while(start > 0 && is_word_char(text[static_cast<size_t>(start) - 1])) --start;
    while(end < n && is_word_char(text[static_cast<size_t>(end)])) ++end;
  }
  return {start, end};
}
} // namespace

void insert_checklist_item_at_cursor(std::string &text, MdFormatState &fmt)
{
  int p = std::max(0, std::min(fmt.cursor_pos, static_cast<int>(text.size())));
  std::string ins = "- [ ] ";
  if(p > 0 && text[static_cast<size_t>(p) - 1] != '\n') ins = "\n" + ins;
  text.insert(static_cast<size_t>(p), ins);
  p += static_cast<int>(ins.size());
  fmt.cursor_pos = p;
  fmt.sel_start = p;
  fmt.sel_end = p;
}

void insert_markdown_table_at_cursor(std::string &text, MdFormatState &fmt, int rows, int cols)
{
  int p = std::max(0, std::min(fmt.cursor_pos, static_cast<int>(text.size())));
  std::string ins;
  if(p > 0 && text[static_cast<size_t>(p) - 1] != '\n') ins.push_back('\n');
  const int safe_rows = std::max(1, rows);
  const int safe_cols = std::max(1, cols);
  const int header_offset = static_cast<int>(ins.size()) + 2;

  ins += "|";
  for(int col = 0; col < safe_cols; ++col)
    ins += " Header " + std::to_string(col + 1) + " |";
  ins += "\n|";
  for(int col = 0; col < safe_cols; ++col)
    ins += " --- |";
  ins += "\n";
  for(int row = 0; row < safe_rows; ++row)
  {
    ins += "|";
    for(int col = 0; col < safe_cols; ++col)
      ins += " Cell " + std::to_string(row + 1) + "," + std::to_string(col + 1) + " |";
    ins += "\n";
  }
  text.insert(static_cast<size_t>(p), ins);

  const int cursor = p + header_offset;
  fmt.cursor_pos = cursor;
  fmt.sel_start = cursor;
  fmt.sel_end = cursor + 8;
  fmt.selection_anchor = cursor;
}

void apply_note_quote(std::string &s, int &sel_a, int &sel_b)
{
  int a = sel_a;
  int b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, static_cast<int>(s.size())));
  b = std::max(0, std::min(b, static_cast<int>(s.size())));

  while(a > 0 && s[static_cast<size_t>(a) - 1] != '\n') --a;
  while(b < static_cast<int>(s.size()) && s[static_cast<size_t>(b)] != '\n') ++b;

  int offset = 0;
  for(int i = a; i <= b;)
  {
    const int insert_pos = i + offset;
    s.insert(static_cast<size_t>(insert_pos), "> ");
    offset += 2;

    const size_t nl = s.find('\n', static_cast<size_t>(insert_pos + 2));
    if(nl == std::string::npos) break;
    i = static_cast<int>(nl) + 1 - offset;
    if(i > b) break;
  }

  sel_a = a;
  sel_b = b + offset;
}

void apply_wrap_string(std::string &s, int &sel_a, int &sel_b,
                       const std::string &left, const std::string &right)
{
  int a = sel_a;
  int b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, static_cast<int>(s.size())));
  b = std::max(0, std::min(b, static_cast<int>(s.size())));

  s.insert(static_cast<size_t>(b), right);
  s.insert(static_cast<size_t>(a), left);

  a += static_cast<int>(left.size());
  b += static_cast<int>(left.size());
  sel_a = a;
  sel_b = b;
}

void apply_color_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &hex_color)
{
  int a = sel_a;
  int b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, static_cast<int>(s.size())));
  b = std::max(0, std::min(b, static_cast<int>(s.size())));

  while(b > a && (s[static_cast<size_t>(b) - 1] == '\n' || s[static_cast<size_t>(b) - 1] == '\r')) --b;
  if(a == b) return;

  sel_a = a;
  sel_b = b;
  apply_wrap_string(s, sel_a, sel_b, "[color=" + hex_color + "]", "[/color]");
}

std::string rgba_to_hex(ImVec4 c)
{
  const int r = static_cast<int>(StringUtils::clamp01f(c.x) * 255.0f + 0.5f);
  const int g = static_cast<int>(StringUtils::clamp01f(c.y) * 255.0f + 0.5f);
  const int b = static_cast<int>(StringUtils::clamp01f(c.z) * 255.0f + 0.5f);

  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return std::string(buf);
}

std::pair<int, int> line_bounds_from_cursor(const std::string &text, int cursor_pos)
{
  int c = std::max(0, std::min(cursor_pos, static_cast<int>(text.size())));
  int line_start = c;
  while(line_start > 0 && text[static_cast<size_t>(line_start) - 1] != '\n') --line_start;

  int line_end = c;
  while(line_end < static_cast<int>(text.size()) && text[static_cast<size_t>(line_end)] != '\n') ++line_end;
  return {line_start, line_end};
}

std::pair<int, int> word_bounds_from_double_click(const std::string &text,
                                                  int cursor_pos,
                                                  int sel_start, int sel_end)
{
  const int n = static_cast<int>(text.size());
  const int cursor = std::max(0, std::min(cursor_pos, n));
  const int sel_lo = std::max(0, std::min(std::min(sel_start, sel_end), n));
  const int sel_hi = std::max(0, std::min(std::max(sel_start, sel_end), n));

  auto try_pos = [&](int pos, std::pair<int, int> &bounds_out) -> bool {
    if(pos < sel_lo || pos >= sel_hi) return false;
    if(pos < 0 || pos >= n) return false;
    if(!is_word_char(text[static_cast<size_t>(pos)])) return false;
    bounds_out = expand_word_bounds(text, pos);
    return true;
  };

  std::pair<int, int> bounds(cursor, cursor);
  if(try_pos(cursor, bounds)) return bounds;
  if(try_pos(cursor - 1, bounds)) return bounds;

  for(int dist = 1; sel_lo + dist <= sel_hi || cursor - dist >= sel_lo; ++dist)
  {
    if(try_pos(cursor - 1 - dist, bounds)) return bounds;
    if(try_pos(cursor + dist, bounds)) return bounds;
  }

  return {cursor, cursor};
}

bool should_push_word_granular_undo(const std::string &before,
                                    const std::string &after,
                                    MdFormatState &st)
{
  const size_t nb = before.size();
  const size_t na = after.size();

  auto reset_groups = [&]() {
    st.typing_word_group = false;
    st.deleting_word_group = false;
  };

  if(before == after) return false;

  size_t i = 0;
  while(i < nb && i < na && before[i] == after[i]) ++i;

  if(na == nb + 1)
  {
    const char c = after[i];
    st.deleting_word_group = false;
    if(!is_word_char(c))
    {
      st.typing_word_group = false;
      st.last_edit_cursor = st.cursor_pos;
      return false;
    }

    const bool contiguous = (st.last_edit_cursor >= 0 && st.cursor_pos == st.last_edit_cursor + 1);
    const bool start_group = !st.typing_word_group || !contiguous;
    st.typing_word_group = true;
    st.last_edit_cursor = st.cursor_pos;
    return start_group;
  }

  if(nb == na + 1)
  {
    const char c = before[i];
    st.typing_word_group = false;
    if(!is_word_char(c))
    {
      st.deleting_word_group = false;
      st.last_edit_cursor = st.cursor_pos;
      return false;
    }

    const bool contiguous =
        (st.last_edit_cursor >= 0) &&
        (st.cursor_pos == st.last_edit_cursor || st.cursor_pos == st.last_edit_cursor - 1);
    const bool start_group = !st.deleting_word_group || !contiguous;
    st.deleting_word_group = true;
    st.last_edit_cursor = st.cursor_pos;
    return start_group;
  }

  reset_groups();
  st.last_edit_cursor = st.cursor_pos;
  return true;
}

void normalize_input_text_buffer(std::string &s)
{
  if(s.empty()) return;
  const size_t max_len = s.capacity() + 1;
  const size_t n = strnlen(s.data(), max_len);
  if(n <= s.size() || n <= s.capacity()) s.resize(n);
}
} // namespace MarkdownEditor