#include "app.hpp"
#include "helpers.hpp"
#include "markdown_view.hpp"
#include "mermaid_flowchart.hpp"
#include "mermaid_flowchart.cpp"

#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <numeric>
#include <utility>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

namespace
{
constexpr const char *kGlslVersion = "#version 150";
constexpr const char *kDefaultStateFile = DATA_PATH "/note.md";
constexpr const char *kLegacyStateMetaFile = DATA_PATH "/current_note_path.txt";
constexpr const char *kIndexFile = DATA_PATH "/notes_index.json";
constexpr const char *kDrawingsFile = DATA_PATH "/drawings_state.txt";
constexpr const char *kClipboardFile = DATA_PATH "/note_clipboard.json";
struct MdSection
{
  int level = 0;               // 1..6
  std::string title;           // heading text
  std::string body;            // markdown until next heading of same/higher level
  std::vector<MdSection> kids; // nested headings
};

struct MdFormatState
{
  int sel_start = 0;
  int sel_end = 0;
  int cursor_pos = 0;

  enum class Action
  {
    None,
    Italic,
    Bold,
    Strike,
    Code,
    Color
  } pending = Action::None;
  ImVec4 color = ImVec4(1, 0.6f, 0.2f, 1); // default

  int selection_anchor = 0;
  int last_cursor_pos = 0;
  bool pending_select_range = false;
  int pending_sel_start = 0;
  int pending_sel_end = 0;
  bool typing_word_group = false;
  bool deleting_word_group = false;
  int last_edit_cursor = -1;
};

struct MdEditorUserData
{
  std::string *text = nullptr;
  MdFormatState *fmt = nullptr;
};

static bool extract_checklist_prefix(std::string_view line, std::string &prefix_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size()) return false;
  const size_t indent_end = i;

  const char bullet = line[i];
  if(bullet != '-' && bullet != '*') return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;
  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;

  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;

  prefix_out.assign(line.substr(0, indent_end));
  prefix_out.push_back(bullet);
  prefix_out.append(" [ ] ");
  return true;
}

static bool extract_quote_prefix(std::string_view line, std::string &prefix_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size()) return false;
  const size_t indent_end = i;
  if(line[i] != '>') return false;
  ++i;
  if(i < line.size() && line[i] == ' ') ++i;

  prefix_out.assign(line.substr(0, indent_end));
  prefix_out.append("> ");
  return true;
}

static bool is_empty_checklist_line(std::string_view line)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size()) return false;
  const char bullet = line[i];
  if(bullet != '-' && bullet != '*') return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;
  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;
  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;
  return trim(line.substr(i)).empty();
}

static bool is_empty_quote_line(std::string_view line)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if(i >= line.size() || line[i] != '>') return false;
  ++i;
  if(i < line.size() && line[i] == ' ') ++i;
  return trim(line.substr(i)).empty();
}

static void insert_checklist_item_at_cursor(std::string &text, MdFormatState &fmt)
{
  int p = std::max(0, std::min(fmt.cursor_pos, (int)text.size()));
  std::string ins = "- [ ] ";
  if(p > 0 && text[(size_t)p - 1] != '\n') ins = "\n" + ins;
  text.insert((size_t)p, ins);
  p += (int)ins.size();
  fmt.cursor_pos = p;
  fmt.sel_start = p;
  fmt.sel_end = p;
}

static void apply_note_quote(std::string &s, int &sel_a, int &sel_b)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Expand to full lines for nicer behavior
  while(a > 0 && s[(size_t)a - 1] != '\n') --a;
  while(b < (int)s.size() && s[(size_t)b] != '\n') ++b;

  // Count lines and insert "> " at each line start.
  // We insert from start to end while tracking the shifting offset.
  int offset = 0;
  for(int i = a; i <= b;)
  {
    const int insert_pos = i + offset;
    s.insert((size_t)insert_pos, "> ");
    offset += 2;

    // Move to next line start
    size_t nl = s.find('\n', (size_t)(insert_pos + 2));
    if(nl == std::string::npos) break;
    i = (int)nl + 1 - offset; // convert back to original coordinate space
    if(i > b) break;
  }

  // Update selection to include the inserted prefixes
  sel_a = a;
  sel_b = b + offset;
}

static void apply_wrap_string(std::string &s, int &sel_a, int &sel_b,
                              const std::string &left, const std::string &right)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);

  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Insert right first (at higher index)
  s.insert((size_t)b, right);
  s.insert((size_t)a, left);

  // Update selection to remain around the original content
  a += (int)left.size();
  b += (int)left.size();
  sel_a = a;
  sel_b = b;
}

static void apply_color_wrap_string(std::string &s, int &sel_a, int &sel_b, const std::string &hex_color)
{
  int a = sel_a, b = sel_b;
  if(a == b) return;
  if(a > b) std::swap(a, b);
  a = std::max(0, std::min(a, (int)s.size()));
  b = std::max(0, std::min(b, (int)s.size()));

  // Keep trailing EOL outside color tags to avoid accidental visual line jumps.
  while(b > a && (s[(size_t)b - 1] == '\n' || s[(size_t)b - 1] == '\r')) --b;
  if(a == b) return;

  sel_a = a;
  sel_b = b;
  apply_wrap_string(s, sel_a, sel_b, "[color=" + hex_color + "]", "[/color]");
}

static std::string rgba_to_hex(ImVec4 c)
{
  auto clamp01 = [](float v) { return v < 0 ? 0.f : (v > 1 ? 1.f : v); };
  int r = (int)(clamp01(c.x) * 255.0f + 0.5f);
  int g = (int)(clamp01(c.y) * 255.0f + 0.5f);
  int b = (int)(clamp01(c.z) * 255.0f + 0.5f);

  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return std::string(buf);
}

static std::pair<int, int> line_bounds_from_cursor(const std::string &text, int cursor_pos)
{
  int c = std::max(0, std::min(cursor_pos, (int)text.size()));
  int line_start = c;
  while(line_start > 0 && text[(size_t)line_start - 1] != '\n') --line_start;

  int line_end = c;
  while(line_end < (int)text.size() && text[(size_t)line_end] != '\n') ++line_end;
  return {line_start, line_end};
}

static bool is_word_char(char c)
{
  const unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) || c == '_';
}

static bool should_push_word_granular_undo(
    const std::string &before,
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

  // Find first differing index.
  size_t i = 0;
  while(i < nb && i < na && before[i] == after[i]) ++i;

  // Single-char insert.
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

  // Single-char delete.
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

  // Paste/replace/multi-char edit: keep as single undo step.
  reset_groups();
  st.last_edit_cursor = st.cursor_pos;
  return true;
}

static int md_editor_cb(ImGuiInputTextCallbackData *data)
{
  auto *st = static_cast<MdFormatState *>(data->UserData);

  if(st->pending_select_range)
  {
    int a = std::max(0, std::min(st->pending_sel_start, data->BufTextLen));
    int b = std::max(0, std::min(st->pending_sel_end, data->BufTextLen));
    data->SelectionStart = a;
    data->SelectionEnd = b;
    data->CursorPos = b;
    st->sel_start = a;
    st->sel_end = b;
    st->cursor_pos = b;
    st->pending_select_range = false;
  }

  // Track selection continuously
  st->sel_start = data->SelectionStart;
  st->sel_end = data->SelectionEnd;
  st->cursor_pos = data->CursorPos;
  if(st->sel_start == st->sel_end) st->selection_anchor = st->cursor_pos;
  st->last_cursor_pos = st->cursor_pos;

  if(data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
  {
    const int c = data->CursorPos;
    if(c > 0 && data->Buf[(size_t)c - 1] == '\n')
    {
      const int line_end = c - 1;
      int line_start = line_end - 1;
      while(line_start >= 0 && data->Buf[(size_t)line_start] != '\n') --line_start;
      ++line_start;

      std::string_view prev(data->Buf + line_start, (size_t)(line_end - line_start));
      std::string prefix;
      if(extract_checklist_prefix(prev, prefix))
      {
        if(is_empty_checklist_line(prev))
        {
          // Enter on empty checklist item exits the list: remove the marker from previous line.
          data->DeleteChars(line_start, line_end - line_start);
          data->CursorPos = line_start + 1; // keep cursor after the newline
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
        else
        {
          data->InsertChars(c, prefix.c_str());
          data->CursorPos = c + (int)prefix.size();
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
      }
      else if(extract_quote_prefix(prev, prefix))
      {
        if(is_empty_quote_line(prev))
        {
          // Enter on empty quote line exits quote mode.
          data->DeleteChars(line_start, line_end - line_start);
          data->CursorPos = line_start + 1;
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
        else
        {
          data->InsertChars(c, prefix.c_str());
          data->CursorPos = c + (int)prefix.size();
          data->SelectionStart = data->SelectionEnd = data->CursorPos;
          st->cursor_pos = data->CursorPos;
          st->sel_start = st->sel_end = st->cursor_pos;
        }
      }
    }
  }

  // // Apply a pending action inside the callback (safe)
  // if(st->pending != MdFormatState::Action::None)
  // {
  //   switch(st->pending)
  //   {
  //   case MdFormatState::Action::Italic:
  //     apply_wrap(data, "*", "*");
  //     break;
  //   case MdFormatState::Action::Bold:
  //     apply_wrap(data, "**", "**");
  //     break;
  //   case MdFormatState::Action::Strike:
  //     apply_wrap(data, "~~", "~~");
  //     break;
  //   case MdFormatState::Action::Code:
  //     apply_wrap(data, "`", "`");
  //     break;
  //   case MdFormatState::Action::Color: {
  //     const std::string hex = rgba_to_hex(st->color);
  //     const std::string left = "[color=" + hex + "]";
  //     const char *right = "[/color]";
  //     apply_wrap(data, left.c_str(), right);
  //   }
  //   break;
  //   default:
  //     break;
  //   }

  //   st->pending = MdFormatState::Action::None;
  // }

  return 0;
}

static void normalize_input_text_buffer(std::string &s)
{
  // ImGui edits the underlying char buffer directly; keep std::string::size() in sync.
  if(s.empty()) return;
  const size_t max_len = s.capacity() + 1;
  const size_t n = strnlen(s.data(), max_len);
  if(n <= s.size() || n <= s.capacity()) s.resize(n);
}

static std::string sanitize_note_filename(std::string title)
{
  for(char &c : title)
  {
    const bool bad =
        c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|';
    if(bad) c = '_';
  }

  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while(!title.empty() && is_space(title.front())) title.erase(title.begin());
  while(!title.empty() && is_space(title.back())) title.pop_back();
  if(title.empty()) title = "note";
  return title;
}

static std::string json_escape(std::string_view s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for(char c : s)
  {
    switch(c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

static std::string json_unescape(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for(size_t i = 0; i < s.size(); ++i)
  {
    if(s[i] == '\\' && i + 1 < s.size())
    {
      char n = s[i + 1];
      if(n == 'n')
        out.push_back('\n');
      else if(n == 'r')
        out.push_back('\r');
      else if(n == 't')
        out.push_back('\t');
      else
        out.push_back(n);
      ++i;
    }
    else
    {
      out.push_back(s[i]);
    }
  }
  return out;
}

static size_t find_matching(std::string_view s, size_t start, char open, char close)
{
  if(start >= s.size() || s[start] != open) return std::string::npos;
  int depth = 0;
  bool in_string = false;
  for(size_t i = start; i < s.size(); ++i)
  {
    char c = s[i];
    if(c == '"' && (i == 0 || s[i - 1] != '\\')) in_string = !in_string;
    if(in_string) continue;
    if(c == open)
      ++depth;
    else if(c == close)
    {
      --depth;
      if(depth == 0) return i;
    }
  }
  return std::string::npos;
}

static std::string json_find_string(std::string_view obj, std::string_view key)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t k = obj.find(pat);
  if(k == std::string::npos) return {};
  size_t q1 = obj.find('"', k + pat.size());
  if(q1 == std::string::npos) return {};
  size_t q2 = q1 + 1;
  while(q2 < obj.size())
  {
    if(obj[q2] == '"' && obj[q2 - 1] != '\\') break;
    ++q2;
  }
  if(q2 >= obj.size()) return {};
  return json_unescape(obj.substr(q1 + 1, q2 - q1 - 1));
}

static int json_find_int(std::string_view obj, std::string_view key, int defv)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t k = obj.find(pat);
  if(k == std::string::npos) return defv;
  size_t c = obj.find(':', k + pat.size());
  if(c == std::string::npos) return defv;
  size_t b = c + 1;
  while(b < obj.size() && (obj[b] == ' ' || obj[b] == '\t' || obj[b] == '\n' || obj[b] == '\r')) ++b;
  size_t e = b;
  while(e < obj.size() && (obj[e] == '-' || (obj[e] >= '0' && obj[e] <= '9'))) ++e;
  if(e <= b) return defv;
  return std::atoi(std::string(obj.substr(b, e - b)).c_str());
}

static bool json_find_bool(std::string_view obj, std::string_view key, bool defv)
{
  const std::string pat = "\"" + std::string(key) + "\"";
  size_t k = obj.find(pat);
  if(k == std::string::npos) return defv;
  size_t c = obj.find(':', k + pat.size());
  if(c == std::string::npos) return defv;
  size_t b = c + 1;
  while(b < obj.size() && (obj[b] == ' ' || obj[b] == '\t' || obj[b] == '\n' || obj[b] == '\r')) ++b;
  if(obj.substr(b, 4) == "true") return true;
  if(obj.substr(b, 5) == "false") return false;
  return defv;
}

static std::vector<std::string_view> json_array_objects(std::string_view arr)
{
  std::vector<std::string_view> out;
  size_t p = 0;
  while(p < arr.size())
  {
    size_t b = arr.find('{', p);
    if(b == std::string::npos) break;
    size_t e = find_matching(arr, b, '{', '}');
    if(e == std::string::npos) break;
    out.push_back(arr.substr(b, e - b + 1));
    p = e + 1;
  }
  return out;
}

static bool parse_heading_line(std::string_view line, int &level_out, std::string_view &title_out)
{
  line = ltrim(line);
  int level = 0;
  while(level < 6 && level < (int)line.size() && line[level] == '#') level++;
  if(level == 0) return false;

  // require a space after hashes (common markdown rule)
  if((size_t)level >= line.size() || line[(size_t)level] != ' ') return false;

  std::string_view title = trim(line.substr((size_t)level + 1));
  if(title.empty()) title = "(untitled)";

  level_out = level;
  title_out = title;
  return true;
}

static MdSection parse_sections(std::string_view md)
{
  MdSection root; // level 0
  std::vector<MdSection *> stack;
  stack.push_back(&root);

  size_t pos = 0;
  auto take_line = [&](size_t &p) -> std::string_view {
    if(p >= md.size()) return {};
    size_t e = md.find('\n', p);
    if(e == std::string_view::npos) e = md.size();
    auto line = md.substr(p, e - p);
    p = (e < md.size()) ? e + 1 : e;
    return line;
  };

  while(pos < md.size())
  {
    std::string_view line = take_line(pos);

    int level = 0;
    std::string_view title;
    if(parse_heading_line(line, level, title))
    {
      // pop until parent has lower level
      while(!stack.empty() && stack.back()->level >= level) stack.pop_back();
      if(stack.empty()) stack.push_back(&root);

      // create node under current parent
      stack.back()->kids.push_back(MdSection{level, std::string(title), {}, {}});
      MdSection *added = &stack.back()->kids.back();
      stack.push_back(added);
    }
    else
    {
      // normal content belongs to current section
      stack.back()->body.append(line.data(), line.size());
      stack.back()->body.push_back('\n');
    }
  }

  return root;
}

static bool parse_task_line(std::string_view line, size_t &check_col_out, std::string_view &label_out)
{
  size_t i = 0;
  while(i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

  if(i >= line.size() || (line[i] != '-' && line[i] != '*')) return false;
  ++i;
  if(i >= line.size() || line[i] != ' ') return false;
  ++i;
  if(i + 2 >= line.size()) return false;
  if(line[i] != '[' || line[i + 2] != ']') return false;

  const char mark = line[i + 1];
  if(mark != ' ' && mark != 'x' && mark != 'X') return false;

  check_col_out = i + 1;
  i += 3;
  if(i < line.size() && line[i] == ' ') ++i;
  label_out = line.substr(i);
  return true;
}

struct MermaidPieSlice
{
  std::string label;
  float value = 0.0f;
};

struct MermaidPieChart
{
  std::string title;
  std::vector<MermaidPieSlice> slices;
};

struct FreeStroke
{
  std::vector<ImVec2> points;
  float thickness = 2.2f;
  ImVec4 color = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
};

static std::unordered_map<std::string, std::vector<FreeStroke>> g_folder_drawings;
static std::unordered_map<std::string, std::vector<std::vector<FreeStroke>>> g_draw_undo;
static std::unordered_map<std::string, std::vector<std::vector<FreeStroke>>> g_draw_redo;
static std::unordered_set<std::string> g_drawings_legacy_checked;
static bool g_drawings_dirty = false;
static bool g_has_copied_note = false;
static std::string g_copied_note_title;
static std::string g_copied_note_content;
struct CopiedNoteItem
{
  std::string title;
  std::string content;
};
struct CopiedFolderEntry
{
  std::string rel_path; // "" for root, otherwise "/child..."
  bool use_custom_color = false;
  float color_r = 0.0f;
  float color_g = 0.0f;
  float color_b = 0.0f;
  std::vector<CopiedNoteItem> notes;
};
static std::vector<CopiedNoteItem> g_copied_notes_batch;
static bool g_has_copied_folder = false;
static std::string g_copied_folder_root_name;
static std::vector<CopiedFolderEntry> g_copied_folder_entries;
static bool g_clipboard_dirty = false;
static std::unordered_map<std::string, GLuint> g_toolbar_icon_cache;

static float dist2(ImVec2 a, ImVec2 b)
{
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return dx * dx + dy * dy;
}

static float clamp01f(float v)
{
  if(v < 0.0f) return 0.0f;
  if(v > 1.0f) return 1.0f;
  return v;
}

struct NoteTheme
{
  ImVec4 window_bg;
  ImVec4 title_bg;
  ImVec4 title_bg_active;
  ImVec4 title_bg_collapsed;
  ImVec4 border;
};

struct SidebarFlash
{
  ImVec4 color;
  double until = 0.0;
};

static ImVec4 mix_color(ImVec4 a, ImVec4 b, float t)
{
  t = clamp01f(t);
  return ImVec4(
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t,
      a.w + (b.w - a.w) * t);
}

static ImVec4 folder_accent_color(bool use_custom_color, float color_r, float color_g, float color_b, const ImGuiStyle &style)
{
  if(use_custom_color) return ImVec4(clamp01f(color_r), clamp01f(color_g), clamp01f(color_b), 1.0f);
  (void)style;
  return ImVec4(0.26f, 0.59f, 0.98f, 1.0f); // ImGui classic default blue
}

static NoteTheme make_note_theme(bool use_custom_color, float color_r, float color_g, float color_b, const ImGuiStyle &style)
{
  const ImVec4 accent = folder_accent_color(use_custom_color, color_r, color_g, color_b, style);
  const ImVec4 base_bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 border = style.Colors[ImGuiCol_Border];

  NoteTheme t;
  t.window_bg = mix_color(base_bg, accent, 0.14f);
  t.window_bg.w = base_bg.w;
  t.title_bg = mix_color(base_bg, accent, 0.44f);
  t.title_bg.w = 1.0f;
  t.title_bg_active = mix_color(base_bg, accent, 0.58f);
  t.title_bg_active.w = 1.0f;
  t.title_bg_collapsed = mix_color(base_bg, accent, 0.34f);
  t.title_bg_collapsed.w = 1.0f;
  t.border = mix_color(border, accent, 0.50f);
  t.border.w = 1.0f;
  return t;
}

static ImVec4 with_alpha(ImVec4 c, float a)
{
  c.w = a;
  return c;
}

static ImTextureID get_toolbar_icon_texture(std::string_view icon_name)
{
  if(icon_name.empty()) return (ImTextureID)0;
  std::string key(icon_name);
  auto it = g_toolbar_icon_cache.find(key);
  if(it != g_toolbar_icon_cache.end()) return (ImTextureID)(uintptr_t)it->second;

  static bool img_ready = []() {
    IMG_Init(IMG_INIT_PNG);
    return true;
  }();
  (void)img_ready;

  const std::filesystem::path p = std::filesystem::path(ASSETS_PATH) / "icons" / key;
  SDL_Surface *loaded = IMG_Load(p.string().c_str());
  if(!loaded) return (ImTextureID)0;
  SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if(!rgba) return (ImTextureID)0;
  // Force toolbar icons to white monochrome while preserving alpha.
  {
    Uint8 *px = static_cast<Uint8 *>(rgba->pixels);
    const int count = rgba->w * rgba->h;
    for(int i = 0; i < count; ++i)
    {
      Uint8 *p4 = px + i * 4; // RGBA32
      if(p4[3] == 0) continue;
      p4[0] = 255;
      p4[1] = 255;
      p4[2] = 255;
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if(tex == 0)
  {
    SDL_FreeSurface(rgba);
    return (ImTextureID)0;
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  SDL_FreeSurface(rgba);

  g_toolbar_icon_cache.emplace(key, tex);
  return (ImTextureID)(uintptr_t)tex;
}

static int push_folder_imgui_theme(const NoteTheme &nt, const ImGuiStyle &style)
{
  const ImVec4 accent = nt.title_bg_active;
  ImVec4 soft = mix_color(nt.window_bg, accent, 0.35f);
  ImVec4 soft_hover = mix_color(nt.window_bg, accent, 0.50f);
  ImVec4 soft_active = mix_color(nt.window_bg, accent, 0.62f);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, nt.window_bg);
  ImGui::PushStyleColor(ImGuiCol_TitleBg, nt.title_bg);
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, nt.title_bg_active);
  ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, nt.title_bg_collapsed);
  ImGui::PushStyleColor(ImGuiCol_Border, nt.border);

  ImGui::PushStyleColor(ImGuiCol_FrameBg, soft);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, soft_hover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, soft_active);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, mix_color(accent, ImVec4(1, 1, 1, 1), 0.35f));

  ImGui::PushStyleColor(ImGuiCol_Button, soft);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, soft_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, soft_active);

  ImGui::PushStyleColor(ImGuiCol_Header, soft);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, soft_hover);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, soft_active);

  ImGui::PushStyleColor(ImGuiCol_Tab, mix_color(style.Colors[ImGuiCol_Tab], accent, 0.45f));
  ImGui::PushStyleColor(ImGuiCol_TabHovered, mix_color(style.Colors[ImGuiCol_TabHovered], accent, 0.45f));
  ImGui::PushStyleColor(ImGuiCol_TabActive, mix_color(style.Colors[ImGuiCol_TabActive], accent, 0.45f));

  ImGui::PushStyleColor(ImGuiCol_SliderGrab, mix_color(accent, ImVec4(1, 1, 1, 1), 0.22f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, mix_color(accent, ImVec4(1, 1, 1, 1), 0.35f));

  return 20;
}

static void load_drawings_state()
{
  g_folder_drawings.clear();
  g_draw_undo.clear();
  g_draw_redo.clear();
  g_drawings_legacy_checked.clear();

  std::ifstream in(kDrawingsFile, std::ios::binary);
  if(!in) return;

  std::string line;
  std::string current_folder;
  while(std::getline(in, line))
  {
    if(line.size() < 2 || line[1] != '\t') continue;

    if(line[0] == 'F')
    {
      current_folder = json_unescape(std::string_view(line).substr(2));
      if(!current_folder.empty() && !g_folder_drawings.count(current_folder))
      {
        g_folder_drawings.emplace(current_folder, std::vector<FreeStroke>{});
      }
      continue;
    }

    if(line[0] != 'S' || current_folder.empty()) continue;

    std::istringstream hs(line.substr(2));
    float thickness = 2.2f;
    float cr = 1.0f, cg = 0.2f, cb = 0.2f, ca = 1.0f;
    int count = 0;
    if(!(hs >> thickness)) continue;

    if(!(hs >> cr >> cg >> cb >> ca >> count))
    {
      hs.clear();
      hs.str(line.substr(2));
      if(!(hs >> thickness >> count) || count <= 0) continue;
      cr = 1.0f;
      cg = 0.2f;
      cb = 0.2f;
      ca = 1.0f;
    }
    if(count <= 0) continue;

    FreeStroke s;
    s.thickness = thickness;
    // Keep strokes clearly visible even if older files saved very low alpha.
    s.color = ImVec4(clamp01f(cr), clamp01f(cg), clamp01f(cb), std::max(0.75f, clamp01f(ca)));
    s.points.reserve((size_t)count);

    for(int i = 0; i < count; ++i)
    {
      std::string pline;
      if(!std::getline(in, pline)) break;
      if(pline.size() < 2 || pline[0] != 'P' || pline[1] != '\t') continue;

      std::istringstream ps(pline.substr(2));
      float x = 0.0f;
      float y = 0.0f;
      if(ps >> x >> y)
      {
        s.points.push_back(ImVec2(x, y));
      }
    }

    if(s.points.size() >= 2) g_folder_drawings[current_folder].push_back(std::move(s));
  }

  g_drawings_dirty = false;
}

static void save_drawings_state()
{
  std::ofstream out(kDrawingsFile, std::ios::trunc);
  if(!out) return;

  for(const auto &kv : g_folder_drawings)
  {
    const std::string &folder = kv.first;
    const auto &strokes = kv.second;
    if(strokes.empty()) continue;

    out << "F\t" << json_escape(folder) << "\n";
    for(const auto &s : strokes)
    {
      if(s.points.size() < 2) continue;
      out << "S\t" << s.thickness
          << "\t" << clamp01f(s.color.x)
          << "\t" << clamp01f(s.color.y)
          << "\t" << clamp01f(s.color.z)
          << "\t" << clamp01f(s.color.w)
          << "\t" << s.points.size() << "\n";
      for(const ImVec2 &p : s.points)
      {
        out << "P\t" << p.x << "\t" << p.y << "\n";
      }
    }
  }

  g_drawings_dirty = false;
}

static void load_note_clipboard()
{
  g_has_copied_note = false;
  g_copied_note_title.clear();
  g_copied_note_content.clear();
  g_copied_notes_batch.clear();

  std::ifstream in(kClipboardFile, std::ios::binary);
  if(!in) return;
  const std::string doc((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  g_has_copied_note = json_find_bool(doc, "has_note", false);
  g_copied_note_title = json_find_string(doc, "title");
  g_copied_note_content = json_find_string(doc, "content");
  if(!g_copied_note_content.empty())
  {
    g_copied_notes_batch.push_back(CopiedNoteItem{g_copied_note_title, g_copied_note_content});
  }
  if(g_copied_notes_batch.empty()) g_has_copied_note = false;
  g_clipboard_dirty = false;
}

static void save_note_clipboard()
{
  std::ofstream out(kClipboardFile, std::ios::trunc);
  if(!out) return;

  out << "{\n";
  out << "  \"has_note\": " << (g_has_copied_note ? "true" : "false") << ",\n";
  out << "  \"title\": \"" << json_escape(g_copied_note_title) << "\",\n";
  out << "  \"content\": \"" << json_escape(g_copied_note_content) << "\"\n";
  out << "}\n";
  g_clipboard_dirty = false;
}

static bool parse_mermaid_pie(std::string_view body, MermaidPieChart &out);
static void render_mermaid_pie_chart(const MermaidPieChart &chart, int id);

static std::string to_lower_copy(std::string_view s)
{
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

static bool is_known_mermaid_type(std::string_view token)
{
  const std::string t = to_lower_copy(token);
  return t == "flowchart" || t == "graph" ||
         t == "sequencediagram" ||
         t == "classdiagram" ||
         t == "statediagram" || t == "statediagram-v2" ||
         t == "erdiagram" ||
         t == "journey" ||
         t == "gantt" ||
         t == "pie" ||
         t == "quadrantchart" ||
         t == "requirementdiagram" ||
         t == "gitgraph" ||
         t == "c4context" || t == "c4container" || t == "c4component" || t == "c4dynamic" || t == "c4deployment" ||
         t == "mindmap" ||
         t == "timeline" ||
         t == "zenuml" ||
         t == "sankey-beta" ||
         t == "xychart-beta" ||
         t == "block-beta" ||
         t == "packet-beta" ||
         t == "kanban" ||
         t == "architecture-beta" ||
         t == "radar-beta" ||
         t == "treemap";
}

static bool detect_mermaid_type(std::string_view body, std::string &type_out)
{
  size_t p = 0;
  while(p < body.size())
  {
    size_t e = body.find('\n', p);
    if(e == std::string_view::npos) e = body.size();
    std::string_view line = trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;
    if(starts_with(line, "%%")) continue;  // comment
    if(starts_with(line, "%%{")) continue; // init block

    size_t sp = line.find_first_of(" \t");
    std::string_view token = (sp == std::string_view::npos) ? line : line.substr(0, sp);
    if(!is_known_mermaid_type(token)) return false;
    type_out = std::string(token);
    return true;
  }
  return false;
}

static void render_mermaid_placeholder(std::string_view type, std::string_view body, int id)
{
  ImGui::PushID(id);
  ImGui::BeginGroup();
  ImGui::Text("Mermaid: %.*s", (int)type.size(), type.data());
  ImGui::Separator();
  ImGui::TextWrapped("%.*s", (int)body.size(), body.data());
  ImGui::EndGroup();
  ImGui::PopID();
}

static void render_mermaid_block(std::string_view mermaid_type, std::string_view body, int id)
{
  const std::string mt = to_lower_copy(mermaid_type);
  if(mt == "pie")
  {
    MermaidPieChart pie;
    if(parse_mermaid_pie(body, pie))
      render_mermaid_pie_chart(pie, id);
    else
      render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }

  if(mt == "flowchart" || mt == "graph")
  {
    MermaidFlowchart::Graph g;
    if(MermaidFlowchart::parse(body, g))
      MermaidFlowchart::render(g, id);
    else
      render_mermaid_placeholder(mermaid_type, body, id);
    return;
  }

  render_mermaid_placeholder(mermaid_type, body, id);
}

static bool parse_mermaid_pie(std::string_view body, MermaidPieChart &out)
{
  out = MermaidPieChart{};
  bool saw_pie = false;

  size_t p = 0;
  while(p < body.size())
  {
    size_t e = body.find('\n', p);
    if(e == std::string_view::npos) e = body.size();
    std::string_view line = trim(body.substr(p, e - p));
    p = (e < body.size()) ? e + 1 : e;

    if(line.empty()) continue;

    if(!saw_pie)
    {
      if(!starts_with(line, "pie")) return false;
      saw_pie = true;
      std::string_view rest = trim(line.substr(3));
      if(starts_with(rest, "title "))
        out.title = std::string(trim(rest.substr(6)));
      continue;
    }

    if(starts_with(line, "title "))
    {
      out.title = std::string(trim(line.substr(6)));
      continue;
    }

    size_t col = line.find(':');
    if(col == std::string_view::npos) continue;

    std::string_view left = trim(line.substr(0, col));
    std::string_view right = trim(line.substr(col + 1));
    if(left.empty() || right.empty()) continue;

    if(left.size() >= 2 && left.front() == '"' && left.back() == '"')
      left = left.substr(1, left.size() - 2);

    std::string right_s(right);
    char *end = nullptr;
    float v = std::strtof(right_s.c_str(), &end);
    if(end == right_s.c_str()) continue;
    if(v <= 0.0f) continue;

    out.slices.push_back({std::string(left), v});
  }

  return saw_pie && !out.slices.empty();
}

static void render_mermaid_pie_chart(const MermaidPieChart &chart, int id)
{
  if(!chart.title.empty()) ImGui::TextUnformatted(chart.title.c_str());

  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float chart_w = std::floor(std::max(120.0f, std::min(240.0f, avail_w * 0.45f)));
  const float chart_h = chart_w;

  ImGui::PushID(id);
  ImGui::BeginGroup();
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##pie_canvas", ImVec2(chart_w, chart_h));
  ImGui::EndGroup();

  ImGui::SameLine();
  ImGui::BeginGroup();

  const float radius = chart_w * 0.5f - 2.0f;
  const ImVec2 center(canvas_pos.x + chart_w * 0.5f, canvas_pos.y + chart_h * 0.5f);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  float total = 0.0f;
  for(const auto &s : chart.slices) total += s.value;
  if(total <= 0.0f) total = 1.0f;

  float a0 = -3.14159265f * 0.5f;
  for(size_t i = 0; i < chart.slices.size(); ++i)
  {
    const auto &s = chart.slices[i];
    const float frac = s.value / total;
    const float a1 = a0 + frac * 2.0f * 3.14159265f;

    float r = 0, g = 0, b = 0;
    ImGui::ColorConvertHSVtoRGB((float)i / std::max(1.0f, (float)chart.slices.size()), 0.65f, 0.95f, r, g, b);
    const ImU32 col = ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));

    const int seg = std::max(6, (int)(36.0f * frac));
    std::vector<ImVec2> pts;
    pts.reserve((size_t)seg + 2);
    pts.push_back(center);
    for(int j = 0; j <= seg; ++j)
    {
      const float t = a0 + (a1 - a0) * ((float)j / (float)seg);
      pts.push_back(ImVec2(center.x + std::cos(t) * radius, center.y + std::sin(t) * radius));
    }
    dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), col);

    ImGui::PushID((int)i);
    ImGui::ColorButton("##c", ImVec4(r, g, b, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(10, 10));
    ImGui::SameLine();
    ImGui::Text("%s : %.2f", s.label.c_str(), s.value);
    ImGui::PopID();

    a0 = a1;
  }

  dl->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), 0, 1.0f);

  ImGui::EndGroup();
  ImGui::PopID();
}

static bool render_preview_with_task_checkboxes(std::string &markdown)
{
  bool changed = false;
  std::string normal_chunk;
  normal_chunk.reserve(markdown.size());
  struct HeaderUi
  {
    int level = 0;
    bool open = false;
  };
  std::vector<HeaderUi> header_stack;

  auto flush_chunk = [&]() {
    if(normal_chunk.empty()) return;
    MarkdownView::render(normal_chunk);
    normal_chunk.clear();
  };

  auto all_headers_open = [&]() -> bool {
    for(const auto &h : header_stack)
    {
      if(!h.open) return false;
    }
    return true;
  };

  size_t pos = 0;
  while(pos < markdown.size())
  {
    const size_t line_start = pos;
    size_t line_end = markdown.find('\n', pos);
    const bool has_newline = (line_end != std::string::npos);
    if(!has_newline) line_end = markdown.size();

    std::string_view line(markdown.data() + line_start, line_end - line_start);
    std::string_view tline = trim(line);

    int heading_level = 0;
    std::string_view heading_title;
    if(parse_heading_line(line, heading_level, heading_title))
    {
      flush_chunk();
      while(!header_stack.empty() && header_stack.back().level >= heading_level)
      {
        if(header_stack.back().open) ImGui::TreePop();
        header_stack.pop_back();
      }
      // If any parent header is closed, nested headers must stay hidden too.
      if(!all_headers_open())
      {
        pos = has_newline ? line_end + 1 : line_end;
        continue;
      }
      ImGuiTreeNodeFlags hf = ImGuiTreeNodeFlags_SpanAvailWidth;
      bool open = ImGui::TreeNodeEx(
          (void *)(intptr_t)((int)line_start + 0x10000),
          hf,
          "%s",
          std::string(heading_title).c_str());
      header_stack.push_back(HeaderUi{heading_level, open});
      pos = has_newline ? line_end + 1 : line_end;
      continue;
    }

    if(!all_headers_open())
    {
      pos = has_newline ? line_end + 1 : line_end;
      continue;
    }

    if(tline == "```mermaid")
    {
      size_t scan = has_newline ? line_end + 1 : line_end;
      size_t block_end = markdown.size();
      std::string body;
      bool closed = false;

      while(scan < markdown.size())
      {
        size_t ls = scan;
        size_t le = markdown.find('\n', scan);
        bool ln = (le != std::string::npos);
        if(!ln) le = markdown.size();

        std::string_view l(markdown.data() + ls, le - ls);
        if(trim(l) == "```")
        {
          block_end = ln ? le + 1 : le;
          closed = true;
          break;
        }
        body.append(l.data(), l.size());
        body.push_back('\n');
        scan = ln ? le + 1 : le;
      }

      if(closed)
      {
        std::string mermaid_type;
        if(detect_mermaid_type(body, mermaid_type))
        {
          flush_chunk();
          render_mermaid_block(mermaid_type, body, (int)line_start);
        }
        else
        {
          normal_chunk.append(markdown.data() + line_start, block_end - line_start);
        }
        pos = block_end;
        continue;
      }
    }

    {
      size_t sp = tline.find_first_of(" \t");
      std::string_view maybe_type = (sp == std::string_view::npos) ? tline : tline.substr(0, sp);
      if(is_known_mermaid_type(maybe_type))
      {
        size_t scan = line_start;
        size_t block_end = markdown.size();
        std::string body;

        while(scan < markdown.size())
        {
          size_t ls = scan;
          size_t le = markdown.find('\n', scan);
          bool ln = (le != std::string::npos);
          if(!ln) le = markdown.size();
          std::string_view l(markdown.data() + ls, le - ls);
          std::string_view tl = trim(l);

          if(tl.empty())
          {
            block_end = ln ? le + 1 : le;
            break;
          }
          body.append(l.data(), l.size());
          body.push_back('\n');
          scan = ln ? le + 1 : le;
          block_end = scan;
        }

        std::string mermaid_type;
        if(detect_mermaid_type(body, mermaid_type))
        {
          flush_chunk();
          render_mermaid_block(mermaid_type, body, (int)line_start);
          pos = block_end;
          continue;
        }
      }
    }

    size_t check_col = 0;
    std::string_view label;
    if(parse_task_line(line, check_col, label))
    {
      flush_chunk();

      bool checked = (line[check_col] == 'x' || line[check_col] == 'X');
      const ImVec2 item_sp = ImGui::GetStyle().ItemSpacing;
      const ImVec2 frame_pad = ImGui::GetStyle().FramePadding;
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_sp.x, 2.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(frame_pad.x, 1.0f));
      ImGui::PushID((int)line_start);
      if(ImGui::Checkbox("##task", &checked))
      {
        markdown[line_start + check_col] = checked ? 'x' : ' ';
        changed = true;
      }
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      MarkdownView::render_inline(std::string(label));
      ImGui::PopID();
      ImGui::PopStyleVar(2);
    }
    else
    {
      normal_chunk.append(line.data(), line.size());
      if(has_newline) normal_chunk.push_back('\n');
    }

    pos = has_newline ? line_end + 1 : line_end;
  }

  flush_chunk();
  while(!header_stack.empty())
  {
    if(header_stack.back().open) ImGui::TreePop();
    header_stack.pop_back();
  }
  return changed;
}

} // namespace

int App::run()
{
  try
  {
    init_sdl_gl();
    init_imgui();
    load_state();

    while(running_)
    {
      frame_begin();
      frame_ui();
      frame_end();
    }

    save_state();
    shutdown();
    return 0;
  }
  catch(const std::exception &e)
  {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
    shutdown();
    return 1;
  }
}

void App::init_sdl_gl()
{
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

  // OpenGL 3.2 Core
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  window_ = SDL_CreateWindow(
      "Notepp",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      1100, 700,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if(!window_)
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

  gl_context_ = SDL_GL_CreateContext(window_);
  if(!gl_context_)
    throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());

  SDL_GL_MakeCurrent(window_, gl_context_);
  SDL_GL_SetSwapInterval(1); // vsync
}

void App::init_imgui()
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();

  // Pick a font family that has Bold/Italic files available.
  io.Fonts->AddFontDefault();

  // Example with Roboto (put these files in assets/fonts or wherever you want):
  // - Roboto-Regular.ttf
  // - Roboto-Italic.ttf
  // - Roboto-Bold.ttf
  // - Roboto-BoldItalic.ttf

  constexpr float kUiFontSize = 14.0f;
  auto merge_emoji_fallback = [&](const char *emoji_font_path) {
    ImFontConfig emoji_cfg;
    emoji_cfg.MergeMode = true;
    emoji_cfg.PixelSnapH = true;
    emoji_cfg.GlyphMinAdvanceX = kUiFontSize; // keep inline width stable
    static const ImWchar emoji_ranges[] = {
        0x200D, 0x200D,   // ZWJ
        0x2600, 0x27BF,   // misc symbols + dingbats
        0xFE0E, 0xFE0F,   // variation selectors
        0x1F300, 0x1FAFF, // emoji blocks
        0};
    io.Fonts->AddFontFromFileTTF(emoji_font_path, kUiFontSize, &emoji_cfg, emoji_ranges);
  };

  ImFont *font_regular = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Medium.ttf", kUiFontSize);
  if(!font_regular) font_regular = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/opensans.ttf", kUiFontSize);
  if(font_regular)
  {
    merge_emoji_fallback(ASSETS_PATH "/fonts/openmojiblack.ttf");
    merge_emoji_fallback(ASSETS_PATH "/fonts/twemoji.ttf");
  }

  ImFont *font_italic = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Italic.ttf", kUiFontSize);
  if(font_italic)
  {
    merge_emoji_fallback(ASSETS_PATH "/fonts/openmojiblack.ttf");
    merge_emoji_fallback(ASSETS_PATH "/fonts/twemoji.ttf");
  }

  ImFont *font_bold = io.Fonts->AddFontFromFileTTF(ASSETS_PATH "/fonts/Roboto-Bold.ttf", kUiFontSize);
  if(font_bold)
  {
    merge_emoji_fallback(ASSETS_PATH "/fonts/openmojiblack.ttf");
    merge_emoji_fallback(ASSETS_PATH "/fonts/twemoji.ttf");
  }

  // Fallback if you don’t have files yet:
  if(!font_regular) font_regular = io.Fonts->Fonts.front();
  if(!font_italic) font_italic = font_regular;
  if(!font_bold) font_bold = font_regular;

  // Ensure all ImGui widgets (including InputTextMultiline editor) use this font atlas entry.
  // Without this, ImGui may keep using AddFontDefault() and render unknown glyphs as '?'.
  io.FontDefault = font_regular;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  if(!ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_))
    throw std::runtime_error("ImGui_ImplSDL2_InitForOpenGL failed");

  if(!ImGui_ImplOpenGL3_Init(kGlslVersion))
    throw std::runtime_error("ImGui_ImplOpenGL3_Init failed");

  MarkdownView::set_fonts(font_regular, font_italic, font_bold);
}

void App::shutdown()
{
  for(auto &kv : g_toolbar_icon_cache)
  {
    if(kv.second != 0)
    {
      GLuint tex = kv.second;
      glDeleteTextures(1, &tex);
    }
  }
  g_toolbar_icon_cache.clear();

  std::unordered_set<std::string> alive_paths;
  for(const FolderMeta &f : folders_)
  {
    for(const NoteMeta &n : f.notes)
    {
      if(!n.path.empty()) alive_paths.insert(n.path);
    }
  }

  for(const std::string &p : pending_fs_delete_paths_)
  {
    if(p.empty()) continue;
    if(alive_paths.find(p) != alive_paths.end()) continue;
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(p), ec);
  }
  pending_fs_delete_paths_.clear();

  // Safe to call multiple times.
  if(ImGui::GetCurrentContext())
  {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }

  if(gl_context_)
  {
    SDL_GL_DeleteContext(gl_context_);
    gl_context_ = nullptr;
  }

  if(window_)
  {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  SDL_Quit();
}

void App::load_state()
{
  folders_.clear();
  pending_fs_delete_paths_.clear();
  active_folder_idx_ = 0;
  active_note_idx_ = 0;
  folder_overview_mode_ = false;

  std::ifstream in_index(kIndexFile);
  if(in_index)
  {
    const std::string doc((std::istreambuf_iterator<char>(in_index)), std::istreambuf_iterator<char>());
    active_folder_idx_ = json_find_int(doc, "active_folder", 0);
    active_note_idx_ = json_find_int(doc, "active_note", 0);
    folder_overview_mode_ = json_find_bool(doc, "folder_view", false);

    const std::string fpat = "\"folders\"";
    size_t fk = doc.find(fpat);
    if(fk != std::string::npos)
    {
      size_t fb = doc.find('[', fk + fpat.size());
      if(fb != std::string::npos)
      {
        size_t fe = find_matching(doc, fb, '[', ']');
        if(fe != std::string::npos)
        {
          std::string_view folder_arr(doc.data() + fb + 1, fe - fb - 1);
          for(std::string_view fobj : json_array_objects(folder_arr))
          {
            FolderMeta f;
            f.name = json_find_string(fobj, "name");
            if(f.name.empty()) f.name = "General";
            f.use_custom_color = json_find_bool(fobj, "use_custom_color", false);
            f.color_r = (float)json_find_int(fobj, "color_r", 0) / 255.0f;
            f.color_g = (float)json_find_int(fobj, "color_g", 0) / 255.0f;
            f.color_b = (float)json_find_int(fobj, "color_b", 0) / 255.0f;

            const std::string npat = "\"notes\"";
            size_t nk = fobj.find(npat);
            if(nk != std::string_view::npos)
            {
              size_t nb = fobj.find('[', nk + npat.size());
              if(nb != std::string_view::npos)
              {
                size_t ne = find_matching(fobj, nb, '[', ']');
                if(ne != std::string_view::npos)
                {
                  std::string_view notes_arr = fobj.substr(nb + 1, ne - nb - 1);
                  for(std::string_view nobj : json_array_objects(notes_arr))
                  {
                    NoteMeta n;
                    n.title = json_find_string(nobj, "title");
                    n.path = json_find_string(nobj, "path");
                    n.pos_x = (float)json_find_int(nobj, "x", 0);
                    n.pos_y = (float)json_find_int(nobj, "y", 0);
                    n.width = (float)json_find_int(nobj, "w", 520);
                    n.height = (float)json_find_int(nobj, "h", 260);
                    n.has_layout = json_find_bool(nobj, "has_layout", false);
                    n.hidden = json_find_bool(nobj, "hidden", false);
                    if(n.title.empty()) n.title = "Note";
                    if(n.path.empty()) n.path = make_note_path(f.name, n.title);
                    f.notes.push_back(std::move(n));
                  }
                }
              }
            }
            folders_.push_back(std::move(f));
          }
        }
      }
    }
  }
  else
  {
    // Migration from legacy current_note_path.txt
    std::string migrated_path;
    std::ifstream legacy(kLegacyStateMetaFile);
    if(legacy)
    {
      std::string p;
      if(std::getline(legacy, p) && !p.empty()) migrated_path = p;
    }

    FolderMeta f;
    f.name = "General";
    if(!migrated_path.empty())
    {
      NoteMeta n;
      n.path = migrated_path;
      const std::filesystem::path fp(migrated_path);
      n.title = fp.stem().empty() ? "Note" : fp.stem().string();
      f.notes.push_back(std::move(n));
    }
    folders_.push_back(std::move(f));
  }

  ensure_default_index();
  normalize_active_indices();
  load_drawings_state();
  load_note_clipboard();
  load_note_content_for_active();
}

void App::save_state() const
{
  if(!state_file_path_.empty())
  {
    std::ofstream out(state_file_path_, std::ios::binary | std::ios::trunc);
    if(out) out << markdown_text_;
  }
  save_index();
  save_drawings_state();
  save_note_clipboard();
}

void App::save_index() const
{
  std::ofstream out(kIndexFile, std::ios::trunc);
  if(!out) return;

  out << "{\n";
  out << "  \"active_folder\": " << active_folder_idx_ << ",\n";
  out << "  \"active_note\": " << active_note_idx_ << ",\n";
  out << "  \"folder_view\": " << (folder_overview_mode_ ? "true" : "false") << ",\n";
  out << "  \"folders\": [\n";
  for(size_t fi = 0; fi < folders_.size(); ++fi)
  {
    const auto &f = folders_[fi];
    out << "    {\n";
    out << "      \"name\": \"" << json_escape(f.name) << "\",\n";
    out << "      \"use_custom_color\": " << (f.use_custom_color ? "true" : "false") << ",\n";
    out << "      \"color_r\": " << (int)std::lround(std::max(0.0f, std::min(1.0f, f.color_r)) * 255.0f) << ",\n";
    out << "      \"color_g\": " << (int)std::lround(std::max(0.0f, std::min(1.0f, f.color_g)) * 255.0f) << ",\n";
    out << "      \"color_b\": " << (int)std::lround(std::max(0.0f, std::min(1.0f, f.color_b)) * 255.0f) << ",\n";
    out << "      \"notes\": [\n";
    for(size_t ni = 0; ni < f.notes.size(); ++ni)
    {
      const auto &n = f.notes[ni];
      out << "        {\"title\": \"" << json_escape(n.title)
          << "\", \"path\": \"" << json_escape(n.path)
          << "\", \"x\": " << (int)std::lround(n.pos_x)
          << ", \"y\": " << (int)std::lround(n.pos_y)
          << ", \"w\": " << (int)std::lround(n.width)
          << ", \"h\": " << (int)std::lround(n.height)
          << ", \"has_layout\": " << (n.has_layout ? "true" : "false")
          << ", \"hidden\": " << (n.hidden ? "true" : "false")
          << "}";
      if(ni + 1 < f.notes.size()) out << ",";
      out << "\n";
    }
    out << "      ]\n";
    out << "    }";
    if(fi + 1 < folders_.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

std::string App::make_note_path(const std::string &folder_name, const std::string &note_title) const
{
  std::string f;
  {
    std::string_view fn(folder_name);
    size_t p = 0;
    bool first = true;
    while(p <= fn.size())
    {
      size_t s = fn.find('/', p);
      if(s == std::string_view::npos) s = fn.size();
      std::string seg = sanitize_note_filename(std::string(fn.substr(p, s - p)));
      if(!seg.empty())
      {
        if(!first) f += "/";
        f += seg;
        first = false;
      }
      if(s == fn.size()) break;
      p = s + 1;
    }
    if(f.empty()) f = "General";
  }
  const std::string n = sanitize_note_filename(note_title);
  std::filesystem::path dir = std::filesystem::path(DATA_PATH) / "notes" / f;
  return (dir / (n + ".md")).string();
}

std::string App::make_unique_note_title(int folder_idx, const std::string &base_title, int ignore_note_idx) const
{
  if(folders_.empty()) return sanitize_note_filename(base_title.empty() ? "Note" : base_title);
  folder_idx = std::max(0, std::min(folder_idx, (int)folders_.size() - 1));
  const FolderMeta &f = folders_[(size_t)folder_idx];

  std::string base = sanitize_note_filename(base_title.empty() ? "Note" : base_title);
  std::string candidate = base;
  int suffix = 2;

  auto exists = [&](const std::string &title) {
    for(int i = 0; i < (int)f.notes.size(); ++i)
    {
      if(i == ignore_note_idx) continue;
      if(f.notes[(size_t)i].title == title) return true;
    }
    return false;
  };

  while(exists(candidate))
  {
    candidate = base + " " + std::to_string(suffix++);
  }
  return candidate;
}

void App::ensure_default_index()
{
  if(folders_.empty()) folders_.push_back(FolderMeta{"General", {}});
  for(auto &f : folders_)
  {
    if(f.name.empty()) f.name = "General";
  }
  normalize_active_indices();
}

void App::normalize_active_indices()
{
  if(folders_.empty()) folders_.push_back(FolderMeta{"General", {}});
  active_folder_idx_ = std::max(0, std::min(active_folder_idx_, (int)folders_.size() - 1));
  const int note_count = (int)folders_[(size_t)active_folder_idx_].notes.size();
  if(note_count <= 0)
  {
    active_note_idx_ = -1;
    return;
  }
  active_note_idx_ = std::max(0, std::min(active_note_idx_, note_count - 1));
}

bool App::has_active_note() const
{
  if(active_folder_idx_ < 0 || active_folder_idx_ >= (int)folders_.size()) return false;
  const auto &notes = folders_[(size_t)active_folder_idx_].notes;
  return active_note_idx_ >= 0 && active_note_idx_ < (int)notes.size();
}

void App::load_note_content_for_active()
{
  ensure_default_index();
  if(!has_active_note())
  {
    state_file_path_.clear();
    note_title_ = "Note";
    markdown_text_.clear();
    undo_stack_.clear();
    redo_stack_.clear();
    request_undo_edit_ = false;
    request_redo_edit_ = false;
    return;
  }

  const NoteMeta &n = folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_];
  state_file_path_ = n.path;
  note_title_ = n.title;

  std::ifstream in(state_file_path_, std::ios::binary);
  if(in)
  {
    markdown_text_.assign((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  }
  else
  {
    std::filesystem::create_directories(std::filesystem::path(state_file_path_).parent_path());
    markdown_text_.clear();
    save_state();
  }
  undo_stack_.clear();
  redo_stack_.clear();
  request_undo_edit_ = false;
  request_redo_edit_ = false;
}

void App::set_active_note(int folder_idx, int note_idx)
{
  ensure_default_index();
  folder_idx = std::max(0, std::min(folder_idx, (int)folders_.size() - 1));
  const int note_count = (int)folders_[(size_t)folder_idx].notes.size();
  if(note_count <= 0)
    note_idx = -1;
  else
    note_idx = std::max(0, std::min(note_idx, note_count - 1));

  save_state();
  active_folder_idx_ = folder_idx;
  active_note_idx_ = note_idx;
  folder_overview_mode_ = false;
  load_note_content_for_active();
  save_index();
}

void App::sync_active_note_meta()
{
  if(!has_active_note()) return;
  FolderMeta &f = folders_[(size_t)active_folder_idx_];
  NoteMeta &n = f.notes[(size_t)active_note_idx_];
  n.title = note_title_;
  n.path = state_file_path_;
  save_index();
}

void App::rename_note_storage_for_title(const std::string &new_title)
{
  if(!has_active_note()) return;
  const std::string safe_title = make_unique_note_title(active_folder_idx_, new_title, active_note_idx_);
  const std::string folder_name = folders_.empty() ? "General" : folders_[(size_t)active_folder_idx_].name;
  std::filesystem::path new_path = make_note_path(folder_name, safe_title);

  if(new_path.string() == state_file_path_)
  {
    note_title_ = safe_title;
    sync_active_note_meta();
    return;
  }

  std::error_code ec;
  std::filesystem::path current_path(state_file_path_);
  if(std::filesystem::exists(current_path, ec))
  {
    if(std::filesystem::exists(new_path, ec))
      std::filesystem::remove(new_path, ec);
    std::filesystem::rename(current_path, new_path, ec);
  }

  state_file_path_ = new_path.string();
  note_title_ = safe_title;
  sync_active_note_meta();
  save_state();
}

void App::rename_note_by_index(int folder_idx, int note_idx, const std::string &new_title)
{
  ensure_default_index();
  folder_idx = std::max(0, std::min(folder_idx, (int)folders_.size() - 1));
  const int note_count = (int)folders_[(size_t)folder_idx].notes.size();
  if(note_count <= 0) return;
  note_idx = std::max(0, std::min(note_idx, note_count - 1));

  const std::string safe_title = make_unique_note_title(folder_idx, new_title, note_idx);
  FolderMeta &f = folders_[(size_t)folder_idx];
  NoteMeta &n = f.notes[(size_t)note_idx];
  std::filesystem::path new_path = make_note_path(f.name, safe_title);

  if(new_path.string() != n.path)
  {
    std::error_code ec;
    std::filesystem::path old_path(n.path);
    if(std::filesystem::exists(old_path, ec))
    {
      if(std::filesystem::exists(new_path, ec))
        std::filesystem::remove(new_path, ec);
      std::filesystem::rename(old_path, new_path, ec);
    }
    n.path = new_path.string();
  }

  n.title = safe_title;
  if(folder_idx == active_folder_idx_ && note_idx == active_note_idx_)
  {
    state_file_path_ = n.path;
    note_title_ = n.title;
  }
  save_index();
}

void App::push_undo_snapshot()
{
  push_undo_snapshot_from(markdown_text_);
}

void App::push_undo_snapshot_from(const std::string &snapshot)
{
  if(!undo_stack_.empty() && undo_stack_.back() == snapshot) return;
  if(undo_stack_.size() >= 64) undo_stack_.erase(undo_stack_.begin());
  undo_stack_.push_back(snapshot);
  redo_stack_.clear();
}

void App::apply_undo_snapshot()
{
  if(undo_stack_.empty()) return;
  if(redo_stack_.size() >= 64) redo_stack_.erase(redo_stack_.begin());
  redo_stack_.push_back(markdown_text_);
  markdown_text_ = undo_stack_.back();
  undo_stack_.pop_back();
  normalize_input_text_buffer(markdown_text_);
  save_state();
}

void App::apply_redo_snapshot()
{
  if(redo_stack_.empty()) return;
  if(undo_stack_.size() >= 64) undo_stack_.erase(undo_stack_.begin());
  undo_stack_.push_back(markdown_text_);
  markdown_text_ = redo_stack_.back();
  redo_stack_.pop_back();
  normalize_input_text_buffer(markdown_text_);
  save_state();
}

void App::frame_begin()
{
  SDL_Event event;
  while(SDL_PollEvent(&event))
  {
    if(event.type == SDL_QUIT) running_ = false;
    if(event.type == SDL_WINDOWEVENT &&
       event.window.event == SDL_WINDOWEVENT_CLOSE &&
       event.window.windowID == SDL_GetWindowID(window_))
    {
      running_ = false;
    }

    // While editing, keep Esc out of InputText so it doesn't cancel/revert the latest edit.
    if(editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_ESCAPE)
    {
      request_exit_edit_mode_ = true;
      continue;
    }
    if(editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       (event.key.keysym.mod & KMOD_CTRL) &&
       event.key.keysym.sym == SDLK_z)
    {
      request_undo_edit_ = true;
      continue;
    }
    if(editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       (event.key.keysym.mod & KMOD_CTRL) &&
       event.key.keysym.sym == SDLK_y)
    {
      request_redo_edit_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_ESCAPE)
    {
      request_clear_selection_ = true;
      request_cancel_draw_tools_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_F2)
    {
      request_rename_selected_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_DELETE)
    {
      request_delete_selected_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       (event.key.keysym.mod & KMOD_CTRL) &&
       event.key.keysym.sym == SDLK_z)
    {
      request_undo_draw_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       (event.key.keysym.mod & KMOD_CTRL) &&
       event.key.keysym.sym == SDLK_y)
    {
      request_redo_draw_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       (event.key.keysym.mod & KMOD_CTRL) &&
       event.key.keysym.sym == SDLK_c)
    {
      request_copy_sidebar_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       (event.key.keysym.mod & KMOD_CTRL) &&
       event.key.keysym.sym == SDLK_v)
    {
      request_paste_sidebar_ = true;
      continue;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
}
void App::frame_ui()
{
  // --- Dock host ---
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoDocking;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##DockHost", nullptr, host_flags);
  ImGui::PopStyleVar(2);

  ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();

  // --- Explorer window: static left sidebar ---
  const float explorer_w = 280.0f;
  const ImVec4 base_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
  const ImVec4 explorer_bg(
      clamp01(base_bg.x + 0.03f),
      clamp01(base_bg.y + 0.03f),
      clamp01(base_bg.z + 0.03f),
      base_bg.w);
  ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(explorer_w, vp->Size.y), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, explorer_bg);
  ImGui::Begin(
      "Explorer",
      nullptr,
      ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleColor();
  static char new_folder_buf[128] = {};
  static char new_note_buf[128] = {};
  static bool open_new_folder_popup = false;
  static bool open_new_note_popup = false;
  static bool focus_new_note_input = false;
  static int new_folder_parent_idx = -1;
  static int new_note_target_folder_idx = -1;
  static int force_open_folder_idx = -1;
  static bool open_rename_note_popup = false;
  static int rename_note_folder_idx = -1;
  static int rename_note_idx = -1;
  static char rename_note_buf[256] = {};
  static bool open_rename_folder_popup = false;
  static int rename_folder_idx = -1;
  static char rename_folder_buf[256] = {};
  static bool open_folder_color_popup = false;
  static int color_folder_idx = -1;
  static float folder_color_buf[3] = {0.0f, 0.0f, 0.0f};
  static bool folder_color_use_default = true;
  static int pending_delete_folder_idx = -1;
  static int pending_delete_note_folder_idx = -1;
  static int pending_delete_note_idx = -1;
  static std::vector<int> pending_delete_note_indices;
  static int pending_paste_note_folder_idx = -1;
  static int paste_target_folder_idx = -1;
  static bool open_paste_note_popup = false;
  static char paste_note_buf[256] = {};
  static std::unordered_set<int> selected_note_indices;
  static std::unordered_set<int> selected_stroke_indices;
  static int pending_focus_note_idx = -1;
  static int last_sidebar_anchor_folder_idx = -1;
  static int last_sidebar_anchor_note_idx = -1;
  static int pending_move_source_folder_idx = -1;
  static int pending_move_target_folder_idx = -1;
  static std::vector<int> pending_move_note_indices;
  static int pending_move_folder_source_idx = -1;
  static int pending_move_folder_target_idx = -1;
  static int drag_hover_folder_idx = -1;
  static std::unordered_map<std::string, SidebarFlash> sidebar_flashes;
  struct SidebarSnapshot
  {
    std::vector<FolderMeta> folders;
    std::unordered_map<std::string, std::vector<FreeStroke>> drawings;
    std::vector<std::string> pending_delete_paths;
    std::unordered_set<int> selected_notes;
    std::unordered_set<int> selected_strokes;
    int active_folder = 0;
    int active_note = -1;
    int pending_focus_note = -1;
    bool folder_overview = true;
    bool editing = false;
  };
  static std::vector<SidebarSnapshot> sidebar_undo;
  static std::vector<SidebarSnapshot> sidebar_redo;

  auto remove_pending_delete_path = [&](const std::string &path) {
    if(path.empty()) return;
    pending_fs_delete_paths_.erase(
        std::remove(pending_fs_delete_paths_.begin(), pending_fs_delete_paths_.end(), path),
        pending_fs_delete_paths_.end());
  };
  auto flash_key_folder = [](const std::string &folder_name) { return std::string("F:") + folder_name; };
  auto flash_key_note = [](const std::string &note_path) { return std::string("N:") + note_path; };
  auto flash_mark = [&](const std::string &key, ImVec4 color, double seconds = 2.4) {
    SidebarFlash fl;
    fl.color = color;
    fl.until = ImGui::GetTime() + seconds;
    sidebar_flashes[key] = fl;
  };
  auto flash_mark_folder = [&](const std::string &folder_name, ImVec4 color, double seconds = 2.4) {
    if(folder_name.empty()) return;
    flash_mark(flash_key_folder(folder_name), color, seconds);
  };
  auto flash_mark_note = [&](const std::string &note_path, ImVec4 color, double seconds = 2.4) {
    if(note_path.empty()) return;
    flash_mark(flash_key_note(note_path), color, seconds);
  };
  auto flash_current_color = [&](const std::string &key, double now) -> ImVec4 {
    auto it = sidebar_flashes.find(key);
    if(it == sidebar_flashes.end()) return ImVec4(0, 0, 0, 0);
    const double rem = it->second.until - now;
    if(rem <= 0.0)
    {
      sidebar_flashes.erase(it);
      return ImVec4(0, 0, 0, 0);
    }
    const float a = clamp01f((float)(rem / 2.4));
    ImVec4 c = it->second.color;
    c.w *= (0.55f + 0.45f * a);
    return c;
  };
  auto queue_pending_delete_path = [&](const std::string &path) {
    if(path.empty()) return;
    if(std::find(pending_fs_delete_paths_.begin(), pending_fs_delete_paths_.end(), path) == pending_fs_delete_paths_.end())
      pending_fs_delete_paths_.push_back(path);
  };
  auto capture_sidebar_snapshot = [&]() -> SidebarSnapshot {
    SidebarSnapshot s;
    s.folders = folders_;
    s.drawings = g_folder_drawings;
    s.pending_delete_paths = pending_fs_delete_paths_;
    s.selected_notes = selected_note_indices;
    s.selected_strokes = selected_stroke_indices;
    s.active_folder = active_folder_idx_;
    s.active_note = active_note_idx_;
    s.pending_focus_note = pending_focus_note_idx;
    s.folder_overview = folder_overview_mode_;
    s.editing = editing_mode_;
    return s;
  };
  auto apply_sidebar_snapshot = [&](const SidebarSnapshot &s) {
    folders_ = s.folders;
    g_folder_drawings = s.drawings;
    pending_fs_delete_paths_ = s.pending_delete_paths;
    selected_note_indices = s.selected_notes;
    selected_stroke_indices = s.selected_strokes;
    active_folder_idx_ = s.active_folder;
    active_note_idx_ = s.active_note;
    pending_focus_note_idx = s.pending_focus_note;
    folder_overview_mode_ = s.folder_overview;
    editing_mode_ = s.editing;
    request_exit_edit_mode_ = false;
    ensure_default_index();
    normalize_active_indices();
    load_note_content_for_active();
    save_index();
    g_drawings_dirty = true;
  };
  auto push_sidebar_snapshot = [&]() {
    if(sidebar_undo.size() >= 64) sidebar_undo.erase(sidebar_undo.begin());
    sidebar_undo.push_back(capture_sidebar_snapshot());
    sidebar_redo.clear();
  };
  auto apply_sidebar_undo = [&]() -> bool {
    if(sidebar_undo.empty()) return false;
    if(sidebar_redo.size() >= 64) sidebar_redo.erase(sidebar_redo.begin());
    sidebar_redo.push_back(capture_sidebar_snapshot());
    SidebarSnapshot prev = sidebar_undo.back();
    sidebar_undo.pop_back();
    apply_sidebar_snapshot(prev);
    if(active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
      flash_mark_folder(folders_[(size_t)active_folder_idx_].name, ImVec4(0.86f, 0.25f, 0.25f, 1.0f));
    return true;
  };
  auto apply_sidebar_redo = [&]() -> bool {
    if(sidebar_redo.empty()) return false;
    if(sidebar_undo.size() >= 64) sidebar_undo.erase(sidebar_undo.begin());
    sidebar_undo.push_back(capture_sidebar_snapshot());
    SidebarSnapshot next = sidebar_redo.back();
    sidebar_redo.pop_back();
    apply_sidebar_snapshot(next);
    if(active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
      flash_mark_folder(folders_[(size_t)active_folder_idx_].name, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
    return true;
  };

  if(request_clear_selection_)
  {
    selected_note_indices.clear();
    selected_stroke_indices.clear();
    pending_focus_note_idx = -1;
    request_clear_selection_ = false;
  }
  drag_hover_folder_idx = -1;

  // Creation is handled from context menus (right-click).

  if(open_new_folder_popup)
  {
    ImGui::OpenPopup("New Folder");
    open_new_folder_popup = false;
    std::snprintf(new_folder_buf, sizeof(new_folder_buf), "Folder");
  }
  if(open_new_note_popup)
  {
    ImGui::OpenPopup("New Note");
    open_new_note_popup = false;
    std::snprintf(new_note_buf, sizeof(new_note_buf), "Note");
    focus_new_note_input = true;
  }
  if(open_rename_note_popup)
  {
    ImGui::OpenPopup("Rename Note Sidebar");
    open_rename_note_popup = false;
  }
  if(open_rename_folder_popup)
  {
    ImGui::OpenPopup("Rename Folder Sidebar");
    open_rename_folder_popup = false;
  }
  if(open_folder_color_popup)
  {
    ImGui::OpenPopup("Folder Color Sidebar");
    open_folder_color_popup = false;
  }
  if(open_paste_note_popup)
  {
    ImGui::OpenPopup("Paste Note");
    open_paste_note_popup = false;
  }

  if(ImGui::BeginPopup("New Folder"))
  {
    ImGui::SetNextItemWidth(200.0f);
    if(ImGui::InputText("Name", new_folder_buf, sizeof(new_folder_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      push_sidebar_snapshot();
      std::string base_name = sanitize_note_filename(new_folder_buf);
      if(base_name.empty()) base_name = "Folder";
      std::string parent_path;
      if(new_folder_parent_idx >= 0 && new_folder_parent_idx < (int)folders_.size())
        parent_path = folders_[(size_t)new_folder_parent_idx].name;
      std::string candidate = parent_path.empty() ? base_name : (parent_path + "/" + base_name);
      int suffix = 2;
      auto folder_exists = [&](const std::string &n) {
        for(const auto &fx : folders_)
        {
          if(fx.name == n) return true;
        }
        return false;
      };
      while(folder_exists(candidate))
      {
        const std::string s = base_name + " " + std::to_string(suffix++);
        candidate = parent_path.empty() ? s : (parent_path + "/" + s);
      }

      FolderMeta f;
      f.name = candidate;
      folders_.push_back(std::move(f));
      save_state();
      active_folder_idx_ = (int)folders_.size() - 1;
      active_note_idx_ = -1;
      folder_overview_mode_ = true;
      flash_mark_folder(candidate, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
      load_note_content_for_active();
      save_index();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("New Note"))
  {
    ImGui::SetNextItemWidth(200.0f);
    if(focus_new_note_input)
    {
      ImGui::SetKeyboardFocusHere();
      focus_new_note_input = false;
    }
    if(ImGui::InputText("Title", new_note_buf, sizeof(new_note_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      push_sidebar_snapshot();
      ensure_default_index();
      const int target_folder_idx =
          (new_note_target_folder_idx >= 0 && new_note_target_folder_idx < (int)folders_.size())
              ? new_note_target_folder_idx
              : active_folder_idx_;
      FolderMeta &f = folders_[(size_t)target_folder_idx];
      NoteMeta n;
      n.title = make_unique_note_title(target_folder_idx, new_note_buf);
      n.path = make_note_path(f.name, n.title);
      remove_pending_delete_path(n.path);
      f.notes.push_back(std::move(n));
      flash_mark_note(f.notes.back().path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
      flash_mark_folder(f.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
      active_folder_idx_ = target_folder_idx;
      active_note_idx_ = (int)f.notes.size() - 1;
      force_open_folder_idx = target_folder_idx;
      save_index();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Rename Note Sidebar"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText("Name", rename_note_buf, sizeof(rename_note_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(rename_note_folder_idx >= 0 && rename_note_idx >= 0)
      {
        push_sidebar_snapshot();
        rename_note_by_index(rename_note_folder_idx, rename_note_idx, rename_note_buf);
        if(rename_note_folder_idx >= 0 && rename_note_folder_idx < (int)folders_.size())
        {
          const FolderMeta &rf = folders_[(size_t)rename_note_folder_idx];
          if(rename_note_idx >= 0 && rename_note_idx < (int)rf.notes.size())
          {
            remove_pending_delete_path(rf.notes[(size_t)rename_note_idx].path);
            flash_mark_note(rf.notes[(size_t)rename_note_idx].path, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
          }
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Rename Folder Sidebar"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText("Name", rename_folder_buf, sizeof(rename_folder_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(rename_folder_idx >= 0 && rename_folder_idx < (int)folders_.size())
      {
        push_sidebar_snapshot();
        FolderMeta &rf = folders_[(size_t)rename_folder_idx];
        const std::string old_name = rf.name;
        const std::string new_name = sanitize_note_filename(rename_folder_buf);
        if(!new_name.empty())
        {
          const size_t slash = old_name.rfind('/');
          const std::string parent = (slash == std::string::npos) ? std::string{} : old_name.substr(0, slash);
          std::string target_name = parent.empty() ? new_name : (parent + "/" + new_name);
          int suffix = 2;
          auto folder_exists = [&](const std::string &n) {
            for(int i = 0; i < (int)folders_.size(); ++i)
            {
              if(i == rename_folder_idx) continue;
              if(folders_[(size_t)i].name == n) return true;
            }
            return false;
          };
          while(folder_exists(target_name))
          {
            const std::string s = new_name + " " + std::to_string(suffix++);
            target_name = parent.empty() ? s : (parent + "/" + s);
          }

          if(target_name != old_name)
          {
            for(NoteMeta &n : rf.notes)
            {
              const std::string new_path = make_note_path(target_name, n.title);
              std::filesystem::create_directories(std::filesystem::path(new_path).parent_path());
              std::error_code ec;
              if(std::filesystem::exists(std::filesystem::path(n.path), ec))
              {
                if(std::filesystem::exists(std::filesystem::path(new_path), ec))
                  std::filesystem::remove(std::filesystem::path(new_path), ec);
                std::filesystem::rename(std::filesystem::path(n.path), std::filesystem::path(new_path), ec);
              }
              n.path = new_path;
            }
            rf.name = target_name;

            auto it = g_folder_drawings.find(old_name);
            if(it != g_folder_drawings.end())
            {
              g_folder_drawings[target_name] = std::move(it->second);
              g_folder_drawings.erase(it);
              g_drawings_dirty = true;
            }
            save_index();
            flash_mark_folder(rf.name, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
            if(rename_folder_idx == active_folder_idx_) load_note_content_for_active();
          }
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Folder Color Sidebar"))
  {
    if(color_folder_idx >= 0 && color_folder_idx < (int)folders_.size())
    {
      FolderMeta &cf = folders_[(size_t)color_folder_idx];
      bool changed = false;
      if(ImGui::Checkbox("Use default", &folder_color_use_default))
      {
        push_sidebar_snapshot();
        changed = true;
      }
      if(!folder_color_use_default)
      {
        if(ImGui::ColorEdit3("Color", folder_color_buf, ImGuiColorEditFlags_NoInputs))
        {
          if(!changed) push_sidebar_snapshot();
          changed = true;
        }
      }
      if(changed)
      {
        cf.use_custom_color = !folder_color_use_default;
        if(!folder_color_use_default)
        {
          cf.color_r = clamp01f(folder_color_buf[0]);
          cf.color_g = clamp01f(folder_color_buf[1]);
          cf.color_b = clamp01f(folder_color_buf[2]);
        }
        flash_mark_folder(cf.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
        save_index();
      }
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Paste Note"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText("Name", paste_note_buf, sizeof(paste_note_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      pending_paste_note_folder_idx = std::max(0, paste_target_folder_idx);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopupContextWindow("ExplorerContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
  {
    if(ImGui::MenuItem("New folder"))
    {
      open_new_folder_popup = true;
      new_folder_parent_idx = -1;
    }
    if(ImGui::MenuItem("New note"))
    {
      open_new_note_popup = true;
      new_note_target_folder_idx = active_folder_idx_;
    }
    if(ImGui::MenuItem("Paste note", nullptr, false, g_has_copied_note))
    {
      paste_target_folder_idx = active_folder_idx_;
      std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
      open_paste_note_popup = true;
    }
    ImGui::EndPopup();
  }

  auto copy_notes_to_internal_clipboard = [&](int folder_idx, const std::vector<int> &indices) {
    if(folder_idx < 0 || folder_idx >= (int)folders_.size()) return;
    const FolderMeta &cf = folders_[(size_t)folder_idx];

    g_copied_notes_batch.clear();
    for(int idx : indices)
    {
      if(idx < 0 || idx >= (int)cf.notes.size()) continue;
      const NoteMeta &n = cf.notes[(size_t)idx];
      std::ifstream in_note(n.path, std::ios::binary);
      std::string content((std::istreambuf_iterator<char>(in_note)), std::istreambuf_iterator<char>());
      g_copied_notes_batch.push_back(CopiedNoteItem{n.title, std::move(content)});
    }

    g_has_copied_note = !g_copied_notes_batch.empty();
    if(g_has_copied_note)
    {
      g_copied_note_title = g_copied_notes_batch.front().title;
      g_copied_note_content = g_copied_notes_batch.front().content;
      g_clipboard_dirty = true;
    }
  };
  auto folder_parent_path = [](const std::string &full) -> std::string {
    const size_t p = full.rfind('/');
    return (p == std::string::npos) ? std::string{} : full.substr(0, p);
  };
  auto folder_base_name = [](const std::string &full) -> std::string {
    const size_t p = full.rfind('/');
    return (p == std::string::npos) ? full : full.substr(p + 1);
  };
  auto folder_exists = [&](const std::string &name) {
    for(const auto &f : folders_)
    {
      if(f.name == name) return true;
    }
    return false;
  };
  auto make_unique_folder_path = [&](const std::string &parent, const std::string &base) {
    std::string b = sanitize_note_filename(base.empty() ? "Folder" : base);
    std::string candidate = parent.empty() ? b : (parent + "/" + b);
    int suffix = 2;
    while(folder_exists(candidate))
    {
      const std::string ss = b + " " + std::to_string(suffix++);
      candidate = parent.empty() ? ss : (parent + "/" + ss);
    }
    return candidate;
  };
  auto copy_folder_to_internal_clipboard = [&](int folder_idx) {
    if(folder_idx < 0 || folder_idx >= (int)folders_.size()) return;
    const std::string root = folders_[(size_t)folder_idx].name;
    g_copied_folder_root_name = root;
    g_copied_folder_entries.clear();
    for(const FolderMeta &f : folders_)
    {
      if(!(f.name == root || starts_with(f.name, root + "/"))) continue;
      CopiedFolderEntry e;
      e.rel_path = f.name.substr(root.size()); // "" or "/child..."
      e.use_custom_color = f.use_custom_color;
      e.color_r = f.color_r;
      e.color_g = f.color_g;
      e.color_b = f.color_b;
      for(const NoteMeta &n : f.notes)
      {
        std::ifstream in_note(n.path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in_note)), std::istreambuf_iterator<char>());
        e.notes.push_back(CopiedNoteItem{n.title, std::move(content)});
      }
      g_copied_folder_entries.push_back(std::move(e));
    }
    g_has_copied_folder = !g_copied_folder_entries.empty();
  };

  if(request_copy_sidebar_)
  {
    if(active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
    {
      if(!selected_note_indices.empty())
      {
        std::vector<int> to_copy(selected_note_indices.begin(), selected_note_indices.end());
        std::sort(to_copy.begin(), to_copy.end());
        copy_notes_to_internal_clipboard(active_folder_idx_, to_copy);
      }
      else if(has_active_note())
      {
        copy_notes_to_internal_clipboard(active_folder_idx_, std::vector<int>{active_note_idx_});
      }
      else
      {
        copy_folder_to_internal_clipboard(active_folder_idx_);
      }
    }
    request_copy_sidebar_ = false;
  }
  if(request_paste_sidebar_)
  {
    if(active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
    {
      // If notes are selected (or active note exists), prefer note paste; otherwise paste folder.
      if(g_has_copied_note && (!selected_note_indices.empty() || has_active_note()))
      {
        pending_paste_note_folder_idx = active_folder_idx_;
      }
      else if(g_has_copied_folder && !g_copied_folder_entries.empty())
      {
        push_sidebar_snapshot();
        const std::string dst_parent = folders_[(size_t)active_folder_idx_].name;
        const std::string new_root = make_unique_folder_path(dst_parent, folder_base_name(g_copied_folder_root_name));
        for(const CopiedFolderEntry &e : g_copied_folder_entries)
        {
          FolderMeta nf;
          nf.name = new_root + e.rel_path;
          nf.use_custom_color = e.use_custom_color;
          nf.color_r = e.color_r;
          nf.color_g = e.color_g;
          nf.color_b = e.color_b;
          std::unordered_set<std::string> used_titles;
          for(const CopiedNoteItem &cn : e.notes)
          {
            NoteMeta nn;
            std::string base_title = sanitize_note_filename(cn.title.empty() ? "Note" : cn.title);
            std::string candidate = base_title;
            int suffix = 2;
            while(used_titles.count(candidate) != 0)
            {
              candidate = base_title + " " + std::to_string(suffix++);
            }
            used_titles.insert(candidate);
            nn.title = candidate;
            nn.path = make_note_path(nf.name, nn.title);
            remove_pending_delete_path(nn.path);
            std::filesystem::create_directories(std::filesystem::path(nn.path).parent_path());
            std::ofstream out_note(nn.path, std::ios::binary | std::ios::trunc);
            if(out_note) out_note << cn.content;
            nf.notes.push_back(std::move(nn));
          }
          folders_.push_back(std::move(nf));
        }
        flash_mark_folder(new_root, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
        save_index();
      }
    }
    request_paste_sidebar_ = false;
  }

  folder_overview_mode_ = true;
  ensure_default_index();
  normalize_active_indices();
  const ImGuiStyle &sidebar_style = ImGui::GetStyle();
  const ImVec4 sidebar_select_gray(0.35f, 0.37f, 0.40f, 1.0f);
  const ImVec4 sidebar_hover_fill = with_alpha(sidebar_select_gray, 0.20f);
  const ImVec4 sidebar_hover_stroke = with_alpha(sidebar_select_gray, 0.82f);
  const ImVec4 sidebar_note_hover_fill = with_alpha(sidebar_select_gray, 0.14f);
  const ImVec4 sidebar_note_hover_stroke = with_alpha(sidebar_select_gray, 0.50f);
  ImGui::PushStyleColor(ImGuiCol_Header, with_alpha(sidebar_select_gray, 0.32f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, with_alpha(sidebar_select_gray, 0.40f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, with_alpha(sidebar_select_gray, 0.50f));
  ImGui::PushStyleColor(ImGuiCol_NavHighlight, with_alpha(sidebar_select_gray, 0.92f));
  const double now_time = ImGui::GetTime();
  std::unordered_map<std::string, std::vector<int>> folder_children;
  folder_children.reserve(folders_.size() * 2 + 4);
  for(int fi = 0; fi < (int)folders_.size(); ++fi)
  {
    folder_children[folder_parent_path(folders_[(size_t)fi].name)].push_back(fi);
  }
  for(auto &kv : folder_children)
  {
    std::sort(kv.second.begin(), kv.second.end(), [&](int a, int b) {
      return folder_base_name(folders_[(size_t)a].name) < folder_base_name(folders_[(size_t)b].name);
    });
  }
  struct SidebarRect
  {
    ImVec2 min;
    ImVec2 max;
    bool valid = false;
  };
  std::vector<SidebarRect> folder_row_rects(folders_.size());
  std::vector<std::vector<SidebarRect>> folder_note_row_rects(folders_.size());
  for(size_t i = 0; i < folders_.size(); ++i) folder_note_row_rects[i].reserve(folders_[i].notes.size());
  auto render_folder_node = [&](auto &&self, int fi) -> void {
    FolderMeta &f = folders_[(size_t)fi];
    if(fi == force_open_folder_idx) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    ImGuiTreeNodeFlags ff =
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if(fi == active_folder_idx_ && folder_overview_mode_) ff |= ImGuiTreeNodeFlags_Selected;
    const std::string base_name = folder_base_name(f.name);
    const ImVec4 tri_col = folder_accent_color(f.use_custom_color, f.color_r, f.color_g, f.color_b, sidebar_style);
    const ImVec4 flash_col = flash_current_color(flash_key_folder(f.name), now_time);
    ImVec4 tree_text_col = tri_col;
    if(flash_col.w > 0.0f) tree_text_col = mix_color(tree_text_col, flash_col, 0.75f);
    tree_text_col.w = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, tree_text_col);
    bool open = ImGui::TreeNodeEx((void *)(intptr_t)(fi + 1), ff, "%s", base_name.c_str());
    ImGui::PopStyleColor();
    folder_row_rects[(size_t)fi] = SidebarRect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true};
    if(drag_hover_folder_idx == fi)
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(
          ImGui::GetItemRectMin(),
          ImGui::GetItemRectMax(),
          ImGui::GetColorU32(sidebar_hover_fill),
          3.0f);
      dl->AddRect(
          ImGui::GetItemRectMin(),
          ImGui::GetItemRectMax(),
          ImGui::GetColorU32(sidebar_hover_stroke),
          3.0f,
          0,
          1.2f);
    }
    if(ImGui::BeginDragDropTarget())
    {
      if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NOTEPP_NOTE_MOVE", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
      {
        if(payload->DataSize == (int)sizeof(ImVec2) && payload->IsPreview())
        {
          drag_hover_folder_idx = fi;
        }
        if(payload->DataSize == (int)sizeof(ImVec2) && payload->IsDelivery())
        {
          const ImVec2 p = *static_cast<const ImVec2 *>(payload->Data);
          pending_move_source_folder_idx = (int)p.x;
          pending_move_target_folder_idx = fi;
          pending_move_note_indices.clear();

          const int dragged_note_idx = (int)p.y;
          if(pending_move_source_folder_idx >= 0 && pending_move_source_folder_idx < (int)folders_.size())
          {
            if(pending_move_source_folder_idx == active_folder_idx_ &&
               selected_note_indices.count(dragged_note_idx) != 0 &&
               selected_note_indices.size() > 1)
            {
              for(int idx : selected_note_indices)
              {
                if(idx >= 0 && idx < (int)folders_[(size_t)pending_move_source_folder_idx].notes.size())
                  pending_move_note_indices.push_back(idx);
              }
            }
            else
            {
              pending_move_note_indices.push_back(dragged_note_idx);
            }
          }
        }
      }
      if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NOTEPP_FOLDER_MOVE", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
      {
        if(payload->DataSize == (int)sizeof(int) && payload->IsPreview()) drag_hover_folder_idx = fi;
        if(payload->DataSize == (int)sizeof(int) && payload->IsDelivery())
        {
          pending_move_folder_source_idx = *static_cast<const int *>(payload->Data);
          pending_move_folder_target_idx = fi;
        }
      }
      ImGui::EndDragDropTarget();
    }
    if(ImGui::BeginDragDropSource())
    {
      int payload = fi;
      ImGui::SetDragDropPayload("NOTEPP_FOLDER_MOVE", &payload, sizeof(payload));
      ImGui::Text("Move folder: %s", folder_base_name(f.name).c_str());
      ImGui::EndDragDropSource();
    }
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
      save_state();
      active_folder_idx_ = fi;
      selected_note_indices.clear();
      selected_stroke_indices.clear();
      folder_overview_mode_ = true;
      editing_mode_ = false;
      request_exit_edit_mode_ = false;
      request_cancel_draw_tools_ = true;
      save_index();
    }

    const std::string folder_popup_id = "FolderCtx##" + std::to_string(fi);
    if(ImGui::BeginPopupContextItem(folder_popup_id.c_str(), ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem("New note"))
      {
        new_note_target_folder_idx = fi;
        open_new_note_popup = true;
      }
      if(ImGui::MenuItem("New folder"))
      {
        open_new_folder_popup = true;
        new_folder_parent_idx = fi;
      }
      if(ImGui::MenuItem("Rename folder"))
      {
        rename_folder_idx = fi;
        std::snprintf(rename_folder_buf, sizeof(rename_folder_buf), "%s", folder_base_name(f.name).c_str());
        open_rename_folder_popup = true;
      }
      if(ImGui::MenuItem("Set folder color..."))
      {
        color_folder_idx = fi;
        folder_color_use_default = !f.use_custom_color;
        folder_color_buf[0] = f.color_r;
        folder_color_buf[1] = f.color_g;
        folder_color_buf[2] = f.color_b;
        open_folder_color_popup = true;
      }
      if(ImGui::MenuItem("Reset folder color", nullptr, false, f.use_custom_color))
      {
        push_sidebar_snapshot();
        f.use_custom_color = false;
        flash_mark_folder(f.name, ImVec4(0.86f, 0.25f, 0.25f, 1.0f));
        save_index();
      }
      if(ImGui::MenuItem("Paste note", nullptr, false, g_has_copied_note))
      {
        paste_target_folder_idx = fi;
        std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
        open_paste_note_popup = true;
      }
      if(ImGui::MenuItem("Remove folder"))
      {
        pending_delete_folder_idx = fi;
      }
      ImGui::EndPopup();
    }

    if(open)
    {
      for(int ni = 0; ni < (int)f.notes.size(); ++ni)
      {
        NoteMeta &n = f.notes[(size_t)ni];
        const bool note_selected =
            (fi == active_folder_idx_) && (selected_note_indices.count(ni) != 0);
        const std::string note_item_label = n.title + "###ExplorerNote_" + std::to_string(fi) + "_" + std::to_string(ni);
        const ImVec4 note_flash_col = flash_current_color(flash_key_note(n.path), now_time);
        if(note_flash_col.w > 0.0f)
        {
          ImVec4 note_text_col = mix_color(ImGui::GetStyleColorVec4(ImGuiCol_Text), note_flash_col, 0.78f);
          note_text_col.w = 1.0f;
          ImGui::PushStyleColor(ImGuiCol_Text, note_text_col);
        }
        if(ImGui::Selectable(note_item_label.c_str(), note_selected))
        {
          const bool ctrl = ImGui::GetIO().KeyCtrl;
          const bool shift = ImGui::GetIO().KeyShift;
          save_state();
          active_folder_idx_ = fi;
          active_note_idx_ = ni;
          n.hidden = false;
          if(shift && last_sidebar_anchor_folder_idx == fi && last_sidebar_anchor_note_idx >= 0)
          {
            int a = std::min(last_sidebar_anchor_note_idx, ni);
            int b = std::max(last_sidebar_anchor_note_idx, ni);
            if(!ctrl)
            {
              selected_note_indices.clear();
              selected_stroke_indices.clear();
            }
            for(int i = a; i <= b; ++i) selected_note_indices.insert(i);
          }
          else if(ctrl)
          {
            if(selected_note_indices.count(ni) != 0)
              selected_note_indices.erase(ni);
            else
              selected_note_indices.insert(ni);
          }
          else
          {
            selected_note_indices.clear();
            selected_note_indices.insert(ni);
            selected_stroke_indices.clear();
          }
          pending_focus_note_idx = ni;
          last_sidebar_anchor_folder_idx = fi;
          last_sidebar_anchor_note_idx = ni;
          force_open_folder_idx = fi;
          editing_mode_ = false;
          request_exit_edit_mode_ = false;
          request_cancel_draw_tools_ = true;
          load_note_content_for_active();
          save_index();
        }
        if(note_flash_col.w > 0.0f) ImGui::PopStyleColor();
        folder_note_row_rects[(size_t)fi].push_back(SidebarRect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true});
        if(drag_hover_folder_idx == fi)
        {
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(
              ImGui::GetItemRectMin(),
              ImGui::GetItemRectMax(),
              ImGui::GetColorU32(sidebar_note_hover_fill),
              2.0f);
        }
        if(ImGui::BeginDragDropTarget())
        {
          if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NOTEPP_NOTE_MOVE", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
          {
            if(payload->DataSize == (int)sizeof(ImVec2) && payload->IsPreview())
            {
              drag_hover_folder_idx = fi;
            }
            if(payload->DataSize == (int)sizeof(ImVec2) && payload->IsDelivery())
            {
              const ImVec2 p = *static_cast<const ImVec2 *>(payload->Data);
              pending_move_source_folder_idx = (int)p.x;
              pending_move_target_folder_idx = fi; // Drop on child note => same destination folder
              pending_move_note_indices.clear();

              const int dragged_note_idx = (int)p.y;
              if(pending_move_source_folder_idx >= 0 && pending_move_source_folder_idx < (int)folders_.size())
              {
                if(pending_move_source_folder_idx == active_folder_idx_ &&
                   selected_note_indices.count(dragged_note_idx) != 0 &&
                   selected_note_indices.size() > 1)
                {
                  for(int idx : selected_note_indices)
                  {
                    if(idx >= 0 && idx < (int)folders_[(size_t)pending_move_source_folder_idx].notes.size())
                      pending_move_note_indices.push_back(idx);
                  }
                }
                else
                {
                  pending_move_note_indices.push_back(dragged_note_idx);
                }
              }
            }
          }
          if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NOTEPP_FOLDER_MOVE", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
          {
            if(payload->DataSize == (int)sizeof(int) && payload->IsPreview()) drag_hover_folder_idx = fi;
            if(payload->DataSize == (int)sizeof(int) && payload->IsDelivery())
            {
              pending_move_folder_source_idx = *static_cast<const int *>(payload->Data);
              pending_move_folder_target_idx = fi;
            }
          }
          ImGui::EndDragDropTarget();
        }
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
          rename_note_folder_idx = fi;
          rename_note_idx = ni;
          std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s", n.title.c_str());
          open_rename_note_popup = true;
        }
        if(ImGui::BeginDragDropSource())
        {
          ImVec2 payload((float)fi, (float)ni);
          ImGui::SetDragDropPayload("NOTEPP_NOTE_MOVE", &payload, sizeof(payload));
          if(selected_note_indices.count(ni) != 0 && fi == active_folder_idx_ && selected_note_indices.size() > 1)
            ImGui::Text("%zu notes", selected_note_indices.size());
          else
            ImGui::TextUnformatted(n.title.c_str());
          ImGui::EndDragDropSource();
        }

        const std::string note_popup_id = "NoteCtx##" + std::to_string(fi) + "_" + std::to_string(ni);
        if(ImGui::BeginPopupContextItem(note_popup_id.c_str(), ImGuiPopupFlags_MouseButtonRight))
        {
          const bool multi_selected_here =
              (fi == active_folder_idx_) &&
              selected_note_indices.count(ni) != 0 &&
              selected_note_indices.size() > 1;
          if(ImGui::MenuItem("Canvia nom"))
          {
            rename_note_folder_idx = fi;
            rename_note_idx = ni;
            std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s", n.title.c_str());
            open_rename_note_popup = true;
          }
          if(ImGui::MenuItem("Edita aquesta nota"))
          {
            save_state();
            active_folder_idx_ = fi;
            active_note_idx_ = ni;
            load_note_content_for_active();
            editing_mode_ = true;
            request_exit_edit_mode_ = false;
            force_open_folder_idx = fi;
            save_index();
          }
          if(ImGui::MenuItem(multi_selected_here ? "Copy selected notes" : "Copy note"))
          {
            std::vector<int> to_copy;
            if(multi_selected_here)
            {
              for(int idx : selected_note_indices) to_copy.push_back(idx);
              std::sort(to_copy.begin(), to_copy.end());
            }
            else
            {
              to_copy.push_back(ni);
            }
            copy_notes_to_internal_clipboard(fi, to_copy);
          }
          if(ImGui::MenuItem("Paste note", nullptr, false, g_has_copied_note))
          {
            paste_target_folder_idx = fi;
            std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
            open_paste_note_popup = true;
          }
          if(ImGui::MenuItem("New note"))
          {
            new_note_target_folder_idx = fi;
            open_new_note_popup = true;
          }
          if(ImGui::MenuItem(multi_selected_here ? "Remove selected notes" : "Elimina nota"))
          {
            pending_delete_note_folder_idx = fi;
            pending_delete_note_indices.clear();
            if(multi_selected_here)
            {
              for(int idx : selected_note_indices) pending_delete_note_indices.push_back(idx);
            }
            else
            {
              pending_delete_note_indices.push_back(ni);
            }
            pending_delete_note_idx = pending_delete_note_indices.empty() ? -1 : pending_delete_note_indices.front();
          }
          ImGui::EndPopup();
        }
      }
      auto itc = folder_children.find(f.name);
      if(itc != folder_children.end())
      {
        for(int cfi : itc->second) self(self, cfi);
      }
      ImGui::TreePop();
    }
  };
  auto roots_it = folder_children.find(std::string{});
  if(roots_it != folder_children.end())
  {
    for(int rfi : roots_it->second) render_folder_node(render_folder_node, rfi);
  }
  if(drag_hover_folder_idx >= 0 && drag_hover_folder_idx < (int)folders_.size())
  {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const SidebarRect &fr = folder_row_rects[(size_t)drag_hover_folder_idx];
    if(fr.valid)
    {
      dl->AddRectFilled(fr.min, fr.max, ImGui::GetColorU32(sidebar_hover_fill), 3.0f);
      dl->AddRect(fr.min, fr.max, ImGui::GetColorU32(sidebar_hover_stroke), 3.0f, 0, 1.2f);
    }
    for(const SidebarRect &nr : folder_note_row_rects[(size_t)drag_hover_folder_idx])
    {
      if(!nr.valid) continue;
      dl->AddRectFilled(nr.min, nr.max, ImGui::GetColorU32(sidebar_note_hover_fill), 2.0f);
      dl->AddRect(nr.min, nr.max, ImGui::GetColorU32(sidebar_note_hover_stroke), 2.0f, 0, 1.0f);
    }
  }
  if(pending_delete_note_folder_idx >= 0 &&
     (!pending_delete_note_indices.empty() || pending_delete_note_idx >= 0))
  {
    push_sidebar_snapshot();
    save_state();
    const int fi = pending_delete_note_folder_idx;
    if(fi >= 0 && fi < (int)folders_.size())
    {
      FolderMeta &df = folders_[(size_t)fi];
      std::vector<int> to_delete = pending_delete_note_indices;
      if(to_delete.empty() && pending_delete_note_idx >= 0) to_delete.push_back(pending_delete_note_idx);
      std::sort(to_delete.begin(), to_delete.end());
      to_delete.erase(std::unique(to_delete.begin(), to_delete.end()), to_delete.end());
      std::sort(to_delete.begin(), to_delete.end(), std::greater<int>());
      for(int ni : to_delete)
      {
        if(ni < 0 || ni >= (int)df.notes.size()) continue;
        queue_pending_delete_path(df.notes[(size_t)ni].path);
        df.notes.erase(df.notes.begin() + ni);
      }
      flash_mark_folder(df.name, ImVec4(0.90f, 0.32f, 0.32f, 1.0f));
      // Keep empty folders valid: deleting last note no longer removes the folder.
    }
    ensure_default_index();
    normalize_active_indices();
    load_note_content_for_active();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    selected_note_indices.clear();
    selected_stroke_indices.clear();
    if(active_note_idx_ >= 0) selected_note_indices.insert(active_note_idx_);
    save_index();
    pending_delete_note_folder_idx = -1;
    pending_delete_note_idx = -1;
    pending_delete_note_indices.clear();
  }
  if(pending_delete_folder_idx >= 0)
  {
    push_sidebar_snapshot();
    save_state();
    const int fi = pending_delete_folder_idx;
    if(fi >= 0 && fi < (int)folders_.size())
    {
      std::string parent_folder_to_mark;
      {
        const std::string name = folders_[(size_t)fi].name;
        const size_t p = name.rfind('/');
        if(p != std::string::npos) parent_folder_to_mark = name.substr(0, p);
      }
      const std::string prefix = folders_[(size_t)fi].name;
      std::vector<int> to_remove;
      for(int i = 0; i < (int)folders_.size(); ++i)
      {
        const std::string &fn = folders_[(size_t)i].name;
        if(fn == prefix || starts_with(fn, prefix + "/")) to_remove.push_back(i);
      }
      std::sort(to_remove.begin(), to_remove.end(), std::greater<int>());
      for(int idx : to_remove)
      {
        FolderMeta &df = folders_[(size_t)idx];
        for(const NoteMeta &n : df.notes)
        {
          queue_pending_delete_path(n.path);
        }
        folders_.erase(folders_.begin() + idx);
      }
      if(!parent_folder_to_mark.empty()) flash_mark_folder(parent_folder_to_mark, ImVec4(0.90f, 0.32f, 0.32f, 1.0f));
    }
    ensure_default_index();
    normalize_active_indices();
    load_note_content_for_active();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    save_index();
    pending_delete_folder_idx = -1;
  }
  if(pending_paste_note_folder_idx >= 0 && g_has_copied_note)
  {
    push_sidebar_snapshot();
    ensure_default_index();
    const int fi = std::max(0, std::min(pending_paste_note_folder_idx, (int)folders_.size() - 1));
    FolderMeta &pf = folders_[(size_t)fi];
    std::vector<CopiedNoteItem> items = g_copied_notes_batch;
    if(items.empty() && !g_copied_note_content.empty())
      items.push_back(CopiedNoteItem{g_copied_note_title, g_copied_note_content});

    if(items.size() <= 1)
    {
      std::string requested = std::string(paste_note_buf);
      if(requested.empty() && !items.empty()) requested = items.front().title;
      std::string base = sanitize_note_filename(requested.empty() ? "Note" : requested);
      std::string candidate = make_unique_note_title(fi, base);
      if(items.empty()) items.push_back(CopiedNoteItem{candidate, ""});

      NoteMeta new_note;
      new_note.title = candidate;
      new_note.path = make_note_path(pf.name, candidate);
      remove_pending_delete_path(new_note.path);
      pf.notes.push_back(new_note);
      flash_mark_note(new_note.path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));

      std::filesystem::create_directories(std::filesystem::path(new_note.path).parent_path());
      std::ofstream out_note(new_note.path, std::ios::binary | std::ios::trunc);
      if(out_note) out_note << items.front().content;
    }
    else
    {
      for(const auto &ci : items)
      {
        const std::string candidate = make_unique_note_title(fi, ci.title.empty() ? "Note" : ci.title);
        NoteMeta new_note;
        new_note.title = candidate;
        new_note.path = make_note_path(pf.name, candidate);
        remove_pending_delete_path(new_note.path);
        pf.notes.push_back(new_note);
        flash_mark_note(new_note.path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));

        std::filesystem::create_directories(std::filesystem::path(new_note.path).parent_path());
        std::ofstream out_note(new_note.path, std::ios::binary | std::ios::trunc);
        if(out_note) out_note << ci.content;
      }
    }

    active_folder_idx_ = fi;
    active_note_idx_ = (int)pf.notes.size() - 1;
    force_open_folder_idx = fi;
    load_note_content_for_active();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    save_index();
    flash_mark_folder(pf.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
    pending_paste_note_folder_idx = -1;
    paste_target_folder_idx = -1;
  }
  if(pending_move_source_folder_idx >= 0 &&
     pending_move_target_folder_idx >= 0 &&
     !pending_move_note_indices.empty())
  {
    push_sidebar_snapshot();
    const int src_fi = pending_move_source_folder_idx;
    const int dst_fi = pending_move_target_folder_idx;
    if(src_fi >= 0 && src_fi < (int)folders_.size() &&
       dst_fi >= 0 && dst_fi < (int)folders_.size() &&
       src_fi != dst_fi)
    {
      FolderMeta &src = folders_[(size_t)src_fi];
      FolderMeta &dst = folders_[(size_t)dst_fi];
      std::sort(pending_move_note_indices.begin(), pending_move_note_indices.end());
      pending_move_note_indices.erase(
          std::unique(pending_move_note_indices.begin(), pending_move_note_indices.end()),
          pending_move_note_indices.end());

      std::vector<int> descending = pending_move_note_indices;
      std::sort(descending.begin(), descending.end(), std::greater<int>());

      std::vector<NoteMeta> moved;
      moved.reserve(descending.size());
      for(int idx : descending)
      {
        if(idx < 0 || idx >= (int)src.notes.size()) continue;
        moved.push_back(src.notes[(size_t)idx]);
        src.notes.erase(src.notes.begin() + idx);
      }
      std::reverse(moved.begin(), moved.end());

      for(auto &nm : moved)
      {
        nm.title = make_unique_note_title(dst_fi, nm.title);
        const std::string new_path = make_note_path(dst.name, nm.title);
        std::filesystem::create_directories(std::filesystem::path(new_path).parent_path());
        std::error_code ec;
        if(std::filesystem::exists(std::filesystem::path(nm.path), ec))
        {
          if(std::filesystem::exists(std::filesystem::path(new_path), ec))
            std::filesystem::remove(std::filesystem::path(new_path), ec);
          std::filesystem::rename(std::filesystem::path(nm.path), std::filesystem::path(new_path), ec);
        }
        nm.path = new_path;
        remove_pending_delete_path(new_path);
        nm.hidden = false;
        dst.notes.push_back(std::move(nm));
        flash_mark_note(dst.notes.back().path, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
      }

      // Source folder may become empty; this is now a valid state.

      active_folder_idx_ = dst_fi;
      active_note_idx_ = dst.notes.empty() ? -1 : ((int)dst.notes.size() - 1);
      selected_note_indices.clear();
      selected_stroke_indices.clear();
      if(active_note_idx_ >= 0) selected_note_indices.insert(active_note_idx_);
      pending_focus_note_idx = active_note_idx_;
      force_open_folder_idx = dst_fi;
      load_note_content_for_active();
      save_index();
      flash_mark_folder(dst.name, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
    }
    pending_move_source_folder_idx = -1;
    pending_move_target_folder_idx = -1;
    pending_move_note_indices.clear();
  }
  if(pending_move_folder_source_idx >= 0 && pending_move_folder_target_idx >= 0)
  {
    push_sidebar_snapshot();
    const int src_fi = pending_move_folder_source_idx;
    const int dst_fi = pending_move_folder_target_idx;
    if(src_fi >= 0 && src_fi < (int)folders_.size() &&
       dst_fi >= 0 && dst_fi < (int)folders_.size() &&
       src_fi != dst_fi)
    {
      const std::string src_name = folders_[(size_t)src_fi].name;
      const std::string dst_name = folders_[(size_t)dst_fi].name;
      if(!(dst_name == src_name || starts_with(dst_name, src_name + "/")))
      {
        auto base_name = [](const std::string &full) {
          const size_t p = full.rfind('/');
          return (p == std::string::npos) ? full : full.substr(p + 1);
        };

        std::string moved_root = dst_name + "/" + base_name(src_name);
        int suffix = 2;
        auto folder_exists = [&](const std::string &name) {
          for(const auto &f : folders_)
          {
            if(f.name == name) return true;
          }
          return false;
        };
        while(folder_exists(moved_root))
        {
          moved_root = dst_name + "/" + base_name(src_name) + " " + std::to_string(suffix++);
        }

        std::vector<int> affected;
        for(int i = 0; i < (int)folders_.size(); ++i)
        {
          const std::string &fn = folders_[(size_t)i].name;
          if(fn == src_name || starts_with(fn, src_name + "/")) affected.push_back(i);
        }

        std::unordered_map<std::string, std::string> name_map;
        for(int idx : affected)
        {
          const std::string old = folders_[(size_t)idx].name;
          const std::string suffix_path = old.substr(src_name.size()); // "" or "/..."
          name_map[old] = moved_root + suffix_path;
        }

        for(int idx : affected)
        {
          FolderMeta &mf = folders_[(size_t)idx];
          const std::string old_name = mf.name;
          const std::string new_name = name_map[old_name];
          for(NoteMeta &n : mf.notes)
          {
            const std::string new_path = make_note_path(new_name, n.title);
            std::filesystem::create_directories(std::filesystem::path(new_path).parent_path());
            std::error_code ec;
            if(std::filesystem::exists(std::filesystem::path(n.path), ec))
            {
              if(std::filesystem::exists(std::filesystem::path(new_path), ec))
                std::filesystem::remove(std::filesystem::path(new_path), ec);
              std::filesystem::rename(std::filesystem::path(n.path), std::filesystem::path(new_path), ec);
            }
            n.path = new_path;
            remove_pending_delete_path(new_path);
          }
          mf.name = new_name;
        }

        std::vector<std::pair<std::string, std::vector<FreeStroke>>> drawings_to_reinsert;
        for(const auto &kv : g_folder_drawings)
        {
          const std::string &k = kv.first;
          if(k == src_name || starts_with(k, src_name + "/"))
          {
            const std::string suffix_path = k.substr(src_name.size());
            drawings_to_reinsert.push_back({moved_root + suffix_path, kv.second});
          }
        }
        for(const auto &kv : drawings_to_reinsert)
        {
          const std::string old_key = src_name + kv.first.substr(moved_root.size());
          g_folder_drawings.erase(old_key);
        }
        for(auto &kv : drawings_to_reinsert)
        {
          g_folder_drawings[kv.first] = std::move(kv.second);
        }
        if(!drawings_to_reinsert.empty()) g_drawings_dirty = true;

        save_index();
        flash_mark_folder(moved_root, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
        if(active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
        {
          load_note_content_for_active();
        }
      }
    }
    pending_move_folder_source_idx = -1;
    pending_move_folder_target_idx = -1;
  }
  force_open_folder_idx = -1;
  ImGui::PopStyleColor(4);
  ImGui::End();

  auto read_file_text = [](const std::string &path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    if(!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  };

  if(folder_overview_mode_)
  {
    static bool refocus_folder_editor = false;
    static int rename_win_folder_idx = -1;
    static int rename_win_note_idx = -1;
    static char rename_win_buf[256] = {};
    static bool focus_rename_win_input = false;
    static bool open_rename_win_popup = false;
    static int anchor_sel_start = 0;
    static int anchor_sel_end = 0;
    static MdFormatState fmt_folder;
    static bool draw_mode = false;
    static bool erase_mode = false;
    static bool stroke_in_progress = false;
    static bool erase_snapshot_taken = false;
    static ImVec4 draw_color = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
    static bool box_selecting = false;
    static ImVec2 box_select_start(0, 0);
    static ImVec2 box_select_end(0, 0);
    static bool box_apply_pending = false;
    static ImVec2 box_apply_start(0, 0);
    static ImVec2 box_apply_end(0, 0);
    static bool select_drag_active = false;
    static ImVec2 select_drag_last_mouse(0, 0);
    static std::string topbar_tooltip_text;
    struct SelectionSnapshot
    {
      std::vector<NoteMeta> notes;
      std::vector<FreeStroke> strokes;
      std::unordered_set<int> selected_notes;
      std::unordered_set<int> selected_strokes;
      int active_note = -1;
    };
    static std::vector<SelectionSnapshot> selection_undo;
    static std::vector<SelectionSnapshot> selection_redo;
    ensure_default_index();
    normalize_active_indices();
    FolderMeta &f = folders_[(size_t)active_folder_idx_];
    const ImVec4 neutral_sel(0.68f, 0.70f, 0.73f, 1.0f);
    for(auto it = selected_note_indices.begin(); it != selected_note_indices.end();)
    {
      const int idx = *it;
      if(idx < 0 || idx >= (int)f.notes.size() || f.notes[(size_t)idx].hidden)
        it = selected_note_indices.erase(it);
      else
        ++it;
    }
    if(request_rename_selected_)
    {
      int target_note_idx = -1;
      if(active_note_idx_ >= 0 && active_note_idx_ < (int)f.notes.size() && !f.notes[(size_t)active_note_idx_].hidden)
      {
        if(selected_note_indices.empty() || selected_note_indices.count(active_note_idx_) != 0)
          target_note_idx = active_note_idx_;
      }
      if(target_note_idx < 0 && !selected_note_indices.empty())
      {
        target_note_idx = *selected_note_indices.begin();
      }

      if(target_note_idx >= 0 && target_note_idx < (int)f.notes.size())
      {
        rename_win_folder_idx = active_folder_idx_;
        rename_win_note_idx = target_note_idx;
        std::snprintf(rename_win_buf, sizeof(rename_win_buf), "%s", f.notes[(size_t)target_note_idx].title.c_str());
        focus_rename_win_input = true;
        open_rename_win_popup = true;
      }
      else
      {
        rename_folder_idx = active_folder_idx_;
        std::snprintf(rename_folder_buf, sizeof(rename_folder_buf), "%s", f.name.c_str());
        open_rename_folder_popup = true;
      }
      request_rename_selected_ = false;
    }
    struct NoteRectInfo
    {
      int idx = -1;
      ImVec2 min;
      ImVec2 max;
    };
    std::vector<NoteRectInfo> note_rects;
    note_rects.reserve(f.notes.size());
    ImVec2 pending_group_delta(0, 0);
    int pending_group_mover = -1;
    if(request_cancel_draw_tools_)
    {
      draw_mode = false;
      erase_mode = false;
      stroke_in_progress = false;
      request_cancel_draw_tools_ = false;
    }
    topbar_tooltip_text.clear();
    auto push_draw_snapshot = [&](const std::string &folder_key) {
      auto &u = g_draw_undo[folder_key];
      auto &r = g_draw_redo[folder_key];
      const auto &cur = g_folder_drawings[folder_key];
      if(u.size() >= 64) u.erase(u.begin());
      u.push_back(cur);
      r.clear();
    };
    auto apply_draw_undo = [&](const std::string &folder_key) {
      auto &u = g_draw_undo[folder_key];
      auto &r = g_draw_redo[folder_key];
      auto &cur = g_folder_drawings[folder_key];
      if(u.empty()) return;
      if(r.size() >= 64) r.erase(r.begin());
      r.push_back(cur);
      cur = u.back();
      u.pop_back();
      g_drawings_dirty = true;
    };
    auto apply_draw_redo = [&](const std::string &folder_key) {
      auto &u = g_draw_undo[folder_key];
      auto &r = g_draw_redo[folder_key];
      auto &cur = g_folder_drawings[folder_key];
      if(r.empty()) return;
      if(u.size() >= 64) u.erase(u.begin());
      u.push_back(cur);
      cur = r.back();
      r.pop_back();
      g_drawings_dirty = true;
    };
    auto capture_selection_snapshot = [&]() -> SelectionSnapshot {
      SelectionSnapshot s;
      s.notes = f.notes;
      s.strokes = g_folder_drawings[f.name];
      s.selected_notes = selected_note_indices;
      s.selected_strokes = selected_stroke_indices;
      s.active_note = active_note_idx_;
      return s;
    };
    auto apply_selection_snapshot = [&](const SelectionSnapshot &s) {
      f.notes = s.notes;
      g_folder_drawings[f.name] = s.strokes;
      selected_note_indices = s.selected_notes;
      selected_stroke_indices = s.selected_strokes;
      if(f.notes.empty())
        active_note_idx_ = -1;
      else
        active_note_idx_ = std::max(0, std::min(s.active_note, (int)f.notes.size() - 1));
      load_note_content_for_active();
      save_index();
      g_drawings_dirty = true;
    };
    auto push_selection_snapshot = [&]() {
      SelectionSnapshot s = capture_selection_snapshot();
      if(selection_undo.size() >= 64) selection_undo.erase(selection_undo.begin());
      selection_undo.push_back(std::move(s));
      selection_redo.clear();
    };
    auto apply_selection_undo = [&]() -> bool {
      if(selection_undo.empty()) return false;
      if(selection_redo.size() >= 64) selection_redo.erase(selection_redo.begin());
      selection_redo.push_back(capture_selection_snapshot());
      SelectionSnapshot prev = selection_undo.back();
      selection_undo.pop_back();
      apply_selection_snapshot(prev);
      return true;
    };
    auto apply_selection_redo = [&]() -> bool {
      if(selection_redo.empty()) return false;
      if(selection_undo.size() >= 64) selection_undo.erase(selection_undo.begin());
      selection_undo.push_back(capture_selection_snapshot());
      SelectionSnapshot next = selection_redo.back();
      selection_redo.pop_back();
      apply_selection_snapshot(next);
      return true;
    };

    {
      const bool has_anchor_selection = (anchor_sel_start != anchor_sel_end);
      const float bar_h = 32.0f;
      ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + explorer_w, vp->Pos.y), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(std::max(200.0f, vp->Size.x - explorer_w), bar_h), ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
      ImGui::Begin(
          "##FormatTopBar",
          nullptr,
          ImGuiWindowFlags_NoTitleBar |
              ImGuiWindowFlags_NoMove |
              ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoSavedSettings |
              ImGuiWindowFlags_NoDocking);
      const ImVec4 top_btn(0.20f, 0.21f, 0.23f, 1.0f);
      const ImVec4 top_btn_hov(0.29f, 0.30f, 0.33f, 1.0f);
      const ImVec4 top_btn_act(0.38f, 0.40f, 0.43f, 1.0f);
      const ImVec4 top_frame(0.16f, 0.17f, 0.19f, 1.0f);
      const ImVec4 top_frame_hov(0.24f, 0.25f, 0.27f, 1.0f);
      const ImVec4 top_frame_act(0.31f, 0.33f, 0.36f, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Button, top_btn);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, top_btn_hov);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, top_btn_act);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, top_frame);
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, top_frame_hov);
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, top_frame_act);
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.32f, 0.36f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.94f, 0.96f, 1.0f));

      if(editing_mode_)
      {
        const ImTextureID ic_italic = get_toolbar_icon_texture("italic.png");
        const ImTextureID ic_bold = get_toolbar_icon_texture("bold.png");
        const ImTextureID ic_strike = get_toolbar_icon_texture("strike.png");
        const ImTextureID ic_note = get_toolbar_icon_texture("note.png");
        const ImTextureID ic_color = get_toolbar_icon_texture("color-brush.png");
        const ImTextureID ic_task = get_toolbar_icon_texture("to-do-list.png");
        auto tool_button = [&](const char *id, ImTextureID tex, const char *fallback, const char *tooltip) -> bool {
          bool pressed = false;
          if(tex)
            pressed = ImGui::ImageButton(id, tex, ImVec2(16.0f, 16.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
          else
            pressed = ImGui::Button(fallback);
          if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) topbar_tooltip_text = tooltip;
          return pressed;
        };
        ImGui::BeginDisabled(!has_anchor_selection);
        if(tool_button("##tb_italic", ic_italic, "Italic", "Italic"))
        {
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
          normalize_input_text_buffer(markdown_text_);
          save_state();
        }
        ImGui::SameLine();
        if(tool_button("##tb_bold", ic_bold, "Bold", "Bold"))
        {
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
          normalize_input_text_buffer(markdown_text_);
          save_state();
        }
        ImGui::SameLine();
        if(tool_button("##tb_strike", ic_strike, "Strike", "Strike"))
        {
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
          normalize_input_text_buffer(markdown_text_);
          save_state();
        }
        ImGui::SameLine();
        if(tool_button("##tb_note", ic_note, "Note", "Note quote"))
        {
          push_undo_snapshot();
          apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
          normalize_input_text_buffer(markdown_text_);
          save_state();
        }
        ImGui::SameLine();
        ImGui::ColorEdit3("##top_color", (float *)&fmt_folder.color, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if(tool_button("##tb_color_apply", ic_color, "Color", "Apply color"))
        {
          push_undo_snapshot();
          const std::string hex = rgba_to_hex(fmt_folder.color);
          apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, hex);
          normalize_input_text_buffer(markdown_text_);
          save_state();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(tool_button("##tb_task", ic_task, "Task", "Task list"))
        {
          push_undo_snapshot();
          insert_checklist_item_at_cursor(markdown_text_, fmt_folder);
          normalize_input_text_buffer(markdown_text_);
          save_state();
        }
        ImGui::SameLine();
      }

      if(!editing_mode_)
      {
        const ImTextureID mouse_icon = get_toolbar_icon_texture("cursor.png");
        const ImTextureID draw_icon = get_toolbar_icon_texture("pencil.png");
        const ImTextureID erase_icon = get_toolbar_icon_texture("erase.png");
        auto mode_button = [&](const char *id, ImTextureID icon, const char *fallback, const char *tooltip, bool active) -> bool {
          if(active)
          {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.47f, 0.49f, 0.53f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.56f, 0.58f, 0.62f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.63f, 0.65f, 0.69f, 1.0f));
          }
          const bool pressed = icon
                                   ? ImGui::ImageButton(id, icon, ImVec2(16.0f, 16.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1))
                                   : ImGui::Button(fallback);
          if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          {
            topbar_tooltip_text = tooltip;
          }
          if(active) ImGui::PopStyleColor(3);
          return pressed;
        };

        const bool mouse_mode = !draw_mode && !erase_mode;
        if(mode_button("##mode_mouse_icon", mouse_icon, "Mouse", "Mouse", mouse_mode))
        {
          draw_mode = false;
          erase_mode = false;
          stroke_in_progress = false;
        }
        ImGui::SameLine();
        if(mode_button("##mode_draw_icon", draw_icon, "Draw", "Draw", draw_mode))
        {
          draw_mode = true;
          erase_mode = false;
          stroke_in_progress = false;
        }
        ImGui::SameLine();
        if(mode_button("##mode_erase_icon", erase_icon, "Erase", "Erase", erase_mode))
        {
          erase_mode = true;
          draw_mode = false;
          stroke_in_progress = false;
        }
        ImGui::SameLine();
      }
      const ImTextureID clear_icon = !editing_mode_ ? get_toolbar_icon_texture("delete-bin.png") : (ImTextureID)0;
      if(!editing_mode_) ImGui::ColorEdit3("##draw_color", (float *)&draw_color, ImGuiColorEditFlags_NoInputs);
      if(!editing_mode_) ImGui::SameLine();
      if(!editing_mode_ &&
         (clear_icon
              ? ImGui::ImageButton("##clear_draw_icon", clear_icon, ImVec2(16.0f, 16.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1))
              : ImGui::Button("Clear drawing")))
      {
        push_draw_snapshot(f.name);
        g_folder_drawings[f.name].clear();
        stroke_in_progress = false;
        g_drawings_dirty = true;
      }
      if(!editing_mode_ && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) topbar_tooltip_text = "Clear drawing";
      ImGui::PopStyleColor(8);
      ImGui::End();
      ImGui::PopStyleVar(2);
    }

    // Notes background interaction layer (right pane below top bar)
    const float top_bar_h = 32.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + explorer_w, vp->Pos.y + top_bar_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(200.0f, vp->Size.x - explorer_w), std::max(100.0f, vp->Size.y - top_bar_h)), ImGuiCond_Always);
    ImGui::Begin(
        "##NotesBackground",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground);
    const ImVec2 bg_wpos = ImGui::GetWindowPos();
    const ImVec2 bg_cmin = ImGui::GetWindowContentRegionMin();
    const ImVec2 bg_cmax = ImGui::GetWindowContentRegionMax();
    const ImVec2 bg_p0(bg_wpos.x + bg_cmin.x, bg_wpos.y + bg_cmin.y);
    const ImVec2 bg_p1(bg_wpos.x + bg_cmax.x, bg_wpos.y + bg_cmax.y);
    const float bg_w = std::max(1.0f, bg_p1.x - bg_p0.x);
    const float bg_h = std::max(1.0f, bg_p1.y - bg_p0.y);
    const bool notes_bg_hovered = ImGui::IsMouseHoveringRect(bg_p0, bg_p1, true);
    auto &folder_strokes = g_folder_drawings[f.name];
    if(!g_drawings_legacy_checked.count(f.name))
    {
      bool looks_legacy = !folder_strokes.empty();
      for(const auto &s : folder_strokes)
      {
        for(const ImVec2 &p : s.points)
        {
          if(p.x < -0.001f || p.y < -0.001f || p.x > 1.001f || p.y > 1.001f)
          {
            looks_legacy = false;
            break;
          }
        }
        if(!looks_legacy) break;
      }
      if(looks_legacy)
      {
        for(auto &s : folder_strokes)
        {
          for(ImVec2 &p : s.points)
          {
            p.x *= bg_w;
            p.y *= bg_h;
          }
        }
        g_drawings_dirty = true;
      }
      g_drawings_legacy_checked.insert(f.name);
    }
    if(request_undo_draw_)
    {
      if(!apply_sidebar_undo())
      {
        if(!apply_selection_undo()) apply_draw_undo(f.name);
      }
      request_undo_draw_ = false;
    }
    if(request_redo_draw_)
    {
      if(!apply_sidebar_redo())
      {
        if(!apply_selection_redo()) apply_draw_redo(f.name);
      }
      request_redo_draw_ = false;
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouse_in_bg_canvas = mouse.x >= bg_p0.x && mouse.x <= bg_p1.x && mouse.y >= bg_p0.y && mouse.y <= bg_p1.y;
    bool mouse_over_note_area = false;
    for(int ni = 0; ni < (int)f.notes.size(); ++ni)
    {
      const NoteMeta &n = f.notes[(size_t)ni];
      if(n.hidden) continue;
      const float nx = n.pos_x;
      const float ny = n.pos_y;
      const float nw = std::max(320.0f, n.width);
      const float nh = std::max(140.0f, n.height);
      if(mouse.x >= nx && mouse.x <= (nx + nw) && mouse.y >= ny && mouse.y <= (ny + nh))
      {
        mouse_over_note_area = true;
        break;
      }
    }

    if((draw_mode || erase_mode) &&
       !editing_mode_ &&
       mouse_in_bg_canvas &&
       ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      if(erase_mode)
      {
        constexpr float eraser_radius = 10.0f;
        const float er2 = eraser_radius * eraser_radius;
        if(!erase_snapshot_taken)
        {
          push_draw_snapshot(f.name);
          erase_snapshot_taken = true;
        }
        const size_t before = folder_strokes.size();
        folder_strokes.erase(
            std::remove_if(folder_strokes.begin(), folder_strokes.end(), [&](const FreeStroke &s) {
              for(const ImVec2 &pn : s.points)
              {
                const ImVec2 ps(bg_p0.x + pn.x, bg_p0.y + pn.y);
                if(dist2(ps, mouse) <= er2) return true;
              }
              return false;
            }),
            folder_strokes.end());
        if(folder_strokes.size() != before) g_drawings_dirty = true;
        stroke_in_progress = false;
      }
      else if(draw_mode)
      {
        if(!stroke_in_progress || folder_strokes.empty())
        {
          push_draw_snapshot(f.name);
          folder_strokes.push_back(FreeStroke{});
          folder_strokes.back().thickness = 2.2f;
          draw_color.w = 1.0f;
          folder_strokes.back().color = draw_color;
          stroke_in_progress = true;
        }

        ImVec2 pn(mouse.x - bg_p0.x, mouse.y - bg_p0.y);
        auto &pts = folder_strokes.back().points;
        if(pts.empty())
        {
          pts.push_back(pn);
          g_drawings_dirty = true;
        }
        else
        {
          const ImVec2 prev_screen(bg_p0.x + pts.back().x, bg_p0.y + pts.back().y);
          if(dist2(prev_screen, mouse) > 2.0f)
          {
            pts.push_back(pn);
            g_drawings_dirty = true;
          }
        }
      }
    }
    else if(stroke_in_progress && draw_mode && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
      if(!folder_strokes.empty() && folder_strokes.back().points.size() < 2)
      {
        folder_strokes.pop_back();
      }
      else if(!folder_strokes.empty())
      {
        g_drawings_dirty = true;
      }
      stroke_in_progress = false;
    }
    if(erase_mode && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) erase_snapshot_taken = false;

    auto stroke_hit_test = [&](int si, ImVec2 m, float rad) -> bool {
      if(si < 0 || si >= (int)folder_strokes.size()) return false;
      const float r2 = rad * rad;
      const auto &s = folder_strokes[(size_t)si];
      for(const ImVec2 &pn : s.points)
      {
        const ImVec2 ps(bg_p0.x + pn.x, bg_p0.y + pn.y);
        if(dist2(ps, m) <= r2) return true;
      }
      return false;
    };

    const bool popup_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if(!draw_mode && !erase_mode && !editing_mode_)
    {
      if(notes_bg_hovered &&
         !mouse_over_note_area &&
         !popup_open &&
         ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        int hovered_selected_stroke = -1;
        int hovered_any_stroke = -1;
        for(int si = (int)folder_strokes.size() - 1; si >= 0; --si)
        {
          if(stroke_hit_test(si, mouse, 8.0f))
          {
            hovered_any_stroke = si;
            if(selected_stroke_indices.count(si) != 0)
            {
              hovered_selected_stroke = si;
              break;
            }
          }
        }

        if(hovered_selected_stroke >= 0 || hovered_any_stroke >= 0)
        {
          if(hovered_selected_stroke < 0 && hovered_any_stroke >= 0)
          {
            if(!ctrl)
            {
              selected_note_indices.clear();
              selected_stroke_indices.clear();
            }
            selected_stroke_indices.insert(hovered_any_stroke);
          }
          push_selection_snapshot();
          select_drag_active = true;
          select_drag_last_mouse = mouse;
        }
        else
        {
          box_selecting = true;
          box_select_start = mouse;
          box_select_end = mouse;
        }
      }
      if(box_selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        box_select_end = mouse;
      }
      if(box_selecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
        box_selecting = false;
        box_apply_pending = true;
        box_apply_start = box_select_start;
        box_apply_end = box_select_end;
      }
    }
    if((draw_mode || erase_mode || editing_mode_) && box_selecting)
    {
      box_selecting = false;
    }
    if(select_drag_active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      const ImVec2 d(mouse.x - select_drag_last_mouse.x, mouse.y - select_drag_last_mouse.y);
      if(std::fabs(d.x) > 0.001f || std::fabs(d.y) > 0.001f)
      {
        for(int idx : selected_note_indices)
        {
          if(idx < 0 || idx >= (int)f.notes.size()) continue;
          NoteMeta &sn = f.notes[(size_t)idx];
          if(sn.hidden) continue;
          sn.pos_x += d.x;
          sn.pos_y += d.y;
          std::string sn_uid = sn.path;
          for(char &c : sn_uid)
          {
            if(c == '#') c = '_';
          }
          const std::string sid = sn.title + "###FolderNote_" + sn_uid;
          ImGui::SetWindowPos(sid.c_str(), ImVec2(sn.pos_x, sn.pos_y), ImGuiCond_Always);
          layout_dirty_ = true;
        }
        for(int si : selected_stroke_indices)
        {
          if(si < 0 || si >= (int)folder_strokes.size()) continue;
          auto &s = folder_strokes[(size_t)si];
          for(ImVec2 &pn : s.points)
          {
            pn.x += d.x;
            pn.y += d.y;
          }
        }
        if(!selected_stroke_indices.empty()) g_drawings_dirty = true;
      }
      select_drag_last_mouse = mouse;
    }
    if(select_drag_active && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
      select_drag_active = false;
    }

    for(auto it = selected_stroke_indices.begin(); it != selected_stroke_indices.end();)
    {
      if(*it < 0 || *it >= (int)folder_strokes.size())
        it = selected_stroke_indices.erase(it);
      else
        ++it;
    }

    {
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      dl->PushClipRect(bg_p0, bg_p1, true);
      std::vector<ImVec2> screen_pts;
      for(int si = 0; si < (int)folder_strokes.size(); ++si)
      {
        const auto &s = folder_strokes[(size_t)si];
        if(s.points.size() < 2) continue;
        screen_pts.clear();
        screen_pts.reserve(s.points.size());
        for(const ImVec2 &pn : s.points)
        {
          screen_pts.push_back(ImVec2(bg_p0.x + pn.x, bg_p0.y + pn.y));
        }
        const bool selected = selected_stroke_indices.count(si) != 0;
        ImVec4 col = selected ? ImVec4(1.0f, 0.92f, 0.15f, 1.0f) : s.color;
        // Keep user-selected hue, only lift brightness a bit for readability.
        float h = 0.0f, sat = 0.0f, val = 0.0f;
        ImGui::ColorConvertRGBtoHSV(col.x, col.y, col.z, h, sat, val);
        val = std::max(val, 0.82f);
        ImGui::ColorConvertHSVtoRGB(h, sat, val, col.x, col.y, col.z);
        col.w = 1.0f;
        const float thick = selected ? std::max(3.0f, s.thickness + 0.8f) : std::max(2.2f, s.thickness);
        if(screen_pts.size() >= 2)
        {
          dl->AddPolyline(screen_pts.data(), (int)screen_pts.size(), ImGui::GetColorU32(col), 0, thick);
        }
      }
      dl->PopClipRect();
    }

    // Stable context target for right-click on empty background.
    ImGui::SetCursorScreenPos(bg_p0);
    ImGui::InvisibleButton("##notes_bg_ctx_target", ImVec2(bg_w, bg_h));
    if(ImGui::BeginPopupContextItem("##notes_bg_ctx", ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem("New note"))
      {
        new_note_target_folder_idx = active_folder_idx_;
        open_new_note_popup = true;
      }
      if(ImGui::MenuItem("Paste note", nullptr, false, g_has_copied_note))
      {
        paste_target_folder_idx = active_folder_idx_;
        std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
        open_paste_note_popup = true;
      }
      ImGui::EndPopup();
    }
    if(box_selecting)
    {
      const ImVec2 rmin(std::min(box_select_start.x, box_select_end.x), std::min(box_select_start.y, box_select_end.y));
      const ImVec2 rmax(std::max(box_select_start.x, box_select_end.x), std::max(box_select_start.y, box_select_end.y));
      ImDrawList *fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilled(rmin, rmax, ImGui::GetColorU32(with_alpha(neutral_sel, 0.18f)));
      fg->AddRect(rmin, rmax, ImGui::GetColorU32(with_alpha(neutral_sel, 0.95f)), 0.0f, 0, 1.5f);
    }
    ImGui::End();

    if(open_rename_win_popup)
    {
      ImGui::OpenPopup("Rename Note Window");
      open_rename_win_popup = false;
    }
    if(ImGui::BeginPopup("Rename Note Window"))
    {
      if(focus_rename_win_input)
      {
        ImGui::SetKeyboardFocusHere();
        focus_rename_win_input = false;
      }
      ImGui::SetNextItemWidth(240.0f);
      if(ImGui::InputText(
             "Name",
             rename_win_buf,
             sizeof(rename_win_buf),
             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
      {
        if(rename_win_folder_idx >= 0 && rename_win_note_idx >= 0)
        {
          rename_note_by_index(rename_win_folder_idx, rename_win_note_idx, rename_win_buf);
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    for(int ni = 0; ni < (int)f.notes.size(); ++ni)
    {
      NoteMeta &n = f.notes[(size_t)ni];
      if(n.hidden) continue;
      const float old_pos_x = n.pos_x;
      const float old_pos_y = n.pos_y;
      std::string note_uid = n.path;
      for(char &c : note_uid)
      {
        if(c == '#') c = '_';
      }
      const std::string window_id = n.title + "###FolderNote_" + note_uid;

      if(n.has_layout)
      {
        ImGui::SetNextWindowPos(ImVec2(n.pos_x, n.pos_y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, n.width), std::max(140.0f, n.height)), ImGuiCond_FirstUseEver);
      }
      else
      {
        const ImVec2 base(340.0f + 40.0f * (float)(ni % 3), 180.0f + 40.0f * (float)(ni % 3));
        ImGui::SetNextWindowPos(base, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 260.0f), ImGuiCond_FirstUseEver);
      }

      bool note_window_open = true;
      ImGuiWindowFlags note_flags =
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
      if(draw_mode || erase_mode) note_flags |= ImGuiWindowFlags_NoInputs;
      if(ni == pending_focus_note_idx) ImGui::SetNextWindowFocus();
      const int folder_theme_count =
          push_folder_imgui_theme(make_note_theme(f.use_custom_color, f.color_r, f.color_g, f.color_b, ImGui::GetStyle()), ImGui::GetStyle());
      ImGui::Begin(
          window_id.c_str(),
          &note_window_open,
          note_flags);
      if(ni == pending_focus_note_idx) pending_focus_note_idx = -1;
      const bool is_editing_this = editing_mode_ && ni == active_note_idx_;

      const ImVec2 win_pos = ImGui::GetWindowPos();
      const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
      const ImVec2 mouse_pos = ImGui::GetMousePos();
      const std::string actions_popup_id = "Note Window Actions##" + note_uid;
      const bool note_is_collapsed = ImGui::IsWindowCollapsed();
      const bool mouse_on_title =
          mouse_pos.x >= win_pos.x &&
          mouse_pos.x <= (win_pos.x + ImGui::GetWindowWidth()) &&
          mouse_pos.y >= win_pos.y &&
          mouse_pos.y <= (win_pos.y + title_bar_h);
      if(mouse_on_title && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        ImGui::OpenPopup(actions_popup_id.c_str());
      }
      if(ImGui::BeginPopup(actions_popup_id.c_str()))
      {
        const bool multi_selected_here =
            selected_note_indices.count(ni) != 0 && selected_note_indices.size() > 1;
        if(ImGui::MenuItem(multi_selected_here ? "Copy selected notes" : "Copy note"))
        {
          std::vector<int> to_copy;
          if(multi_selected_here)
          {
            for(int idx : selected_note_indices) to_copy.push_back(idx);
            std::sort(to_copy.begin(), to_copy.end());
          }
          else
          {
            to_copy.push_back(ni);
          }
          copy_notes_to_internal_clipboard(active_folder_idx_, to_copy);
        }
        if(ImGui::MenuItem("Rename"))
        {
          rename_win_folder_idx = active_folder_idx_;
          rename_win_note_idx = ni;
          std::snprintf(rename_win_buf, sizeof(rename_win_buf), "%s", n.title.c_str());
          focus_rename_win_input = true;
          open_rename_win_popup = true;
        }
        if(ImGui::MenuItem("Edit"))
        {
          if(editing_mode_ && !is_editing_this)
          {
            normalize_input_text_buffer(markdown_text_);
            save_state();
            editing_mode_ = false;
          }
          if(!is_editing_this)
          {
            active_note_idx_ = ni;
            selected_note_indices.clear();
            selected_note_indices.insert(ni);
            selected_stroke_indices.clear();
            load_note_content_for_active();
            editing_mode_ = true;
            request_exit_edit_mode_ = false;
            refocus_folder_editor = true;
            save_index();
          }
        }
        if(ImGui::MenuItem(note_is_collapsed ? "Expand" : "Compact"))
        {
          ImGui::SetWindowCollapsed(window_id.c_str(), !note_is_collapsed, ImGuiCond_Always);
        }
        if(ImGui::MenuItem("Hide"))
        {
          n.hidden = true;
          if(is_editing_this)
          {
            normalize_input_text_buffer(markdown_text_);
            save_state();
            editing_mode_ = false;
          }
          save_index();
        }
        if(ImGui::MenuItem(multi_selected_here ? "Remove selected notes" : "Remove note"))
        {
          pending_delete_note_folder_idx = active_folder_idx_;
          pending_delete_note_indices.clear();
          if(multi_selected_here)
          {
            for(int idx : selected_note_indices) pending_delete_note_indices.push_back(idx);
          }
          else
          {
            pending_delete_note_indices.push_back(ni);
          }
          pending_delete_note_idx = pending_delete_note_indices.empty() ? -1 : pending_delete_note_indices.front();
        }
        ImGui::EndPopup();
      }

      bool changed = false;
      std::string preview_text;

      if(is_editing_this)
      {
        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_AllowTabInput |
            ImGuiInputTextFlags_CallbackResize |
            ImGuiInputTextFlags_CallbackEdit |
            ImGuiInputTextFlags_CallbackAlways;
        static MdEditorUserData ud_folder{&markdown_text_, &fmt_folder};
        ud_folder.text = &markdown_text_;

        if(refocus_folder_editor)
        {
          ImGui::SetKeyboardFocusHere();
          refocus_folder_editor = false;
        }

        const std::string before_edit = markdown_text_;
        changed = ImGui::InputTextMultiline(
            "##md_folder",
            markdown_text_.data(),
            markdown_text_.capacity() + 1,
            ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
            flags,
            [](ImGuiInputTextCallbackData *data) -> int {
              auto *ud = static_cast<MdEditorUserData *>(data->UserData);
              if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
              {
                ud->text->resize((size_t)data->BufTextLen);
                data->Buf = ud->text->data();
                return 0;
              }
              data->UserData = ud->fmt;
              return md_editor_cb(data);
            },
            &ud_folder);
        normalize_input_text_buffer(markdown_text_);
        if(changed)
        {
          if(should_push_word_granular_undo(before_edit, markdown_text_, fmt_folder))
            push_undo_snapshot_from(before_edit);
          save_state();
        }
        if(request_undo_edit_)
        {
          apply_undo_snapshot();
          request_undo_edit_ = false;
          request_redo_edit_ = false;
        }
        if(request_redo_edit_)
        {
          apply_redo_snapshot();
          request_redo_edit_ = false;
        }

        const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const ImGuiIO &io = ImGui::GetIO();
        if(editor_hovered && io.MouseClickedCount[ImGuiMouseButton_Left] >= 3)
        {
          const auto [ls, le] = line_bounds_from_cursor(markdown_text_, fmt_folder.cursor_pos);
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = ls;
          fmt_folder.pending_sel_end = le;
          fmt_folder.selection_anchor = ls;
        }
        if(editor_hovered && io.KeyCtrl && io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = fmt_folder.selection_anchor;
          fmt_folder.pending_sel_end = fmt_folder.cursor_pos;
        }
        const int a = fmt_folder.sel_start;
        const int b = fmt_folder.sel_end;
        const bool has_selection = (a != b);
        const int sel_min = (a < b) ? a : b;
        const int sel_max = (a < b) ? b : a;
        if(has_selection)
        {
          anchor_sel_start = sel_min;
          anchor_sel_end = sel_max;
        }
        (void)editor_hovered;
      }
      else
      {
        preview_text = read_file_text(n.path);
        const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f);
        MarkdownView::set_render_width(preview_w);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        changed = render_preview_with_task_checkboxes(preview_text);
        ImGui::PopTextWrapPos();
        if(changed)
        {
          if(ni == active_note_idx_)
          {
            markdown_text_ = preview_text;
            normalize_input_text_buffer(markdown_text_);
            save_state();
          }
          else
          {
            std::ofstream out(n.path, std::ios::binary | std::ios::trunc);
            if(out) out << preview_text;
          }
        }
      }

      const std::string body_popup_id = "Note Body Actions##" + note_uid;
      const bool w_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      if(!is_editing_this && w_hovered && !mouse_on_title && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        ImGui::OpenPopup(body_popup_id.c_str());
      }
      if(!is_editing_this && ImGui::BeginPopup(body_popup_id.c_str()))
      {
        if(ImGui::MenuItem("Copy all"))
        {
          ImGui::SetClipboardText(preview_text.c_str());
        }
        ImGui::EndPopup();
      }

      if(w_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        active_note_idx_ = ni;
        if(ctrl)
        {
          if(selected_note_indices.count(ni) != 0)
            selected_note_indices.erase(ni);
          else
            selected_note_indices.insert(ni);
        }
        else
        {
          // Keep current multi-selection when grabbing one of the selected notes.
          if(selected_note_indices.count(ni) == 0)
          {
            selected_note_indices.clear();
            selected_note_indices.insert(ni);
            selected_stroke_indices.clear();
          }
        }
      }
      if(w_hovered && !mouse_on_title && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
        if(editing_mode_ && !is_editing_this)
        {
          normalize_input_text_buffer(markdown_text_);
          save_state();
          editing_mode_ = false;
        }
        if(!is_editing_this)
        {
          active_note_idx_ = ni;
          selected_note_indices.clear();
          selected_note_indices.insert(ni);
          selected_stroke_indices.clear();
          load_note_content_for_active();
          editing_mode_ = true;
          request_exit_edit_mode_ = false;
          refocus_folder_editor = true;
          save_index();
        }
      }

      const float auto_h = std::max(140.0f, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
      const ImVec2 pos = ImGui::GetWindowPos();
      const ImVec2 size = ImGui::GetWindowSize();
      note_rects.push_back(NoteRectInfo{ni, pos, ImVec2(pos.x + size.x, pos.y + size.y)});
      if(selected_note_indices.count(ni) != 0)
      {
        ImGui::GetForegroundDrawList()->AddRect(
            pos,
            ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::GetColorU32(with_alpha(neutral_sel, 0.95f)),
            0.0f,
            0,
            2.0f);
      }

      if(!is_editing_this)
      {
        ImGui::SetWindowSize(ImVec2(size.x, auto_h));
      }

      const ImVec2 note_delta(pos.x - old_pos_x, pos.y - old_pos_y);
      if(selected_note_indices.count(ni) != 0 &&
         (std::fabs(note_delta.x) > 0.01f || std::fabs(note_delta.y) > 0.01f) &&
         ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
      {
        pending_group_delta = note_delta;
        pending_group_mover = ni;
      }

      auto changed_f = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
      if(changed_f(n.pos_x, pos.x) || changed_f(n.pos_y, pos.y) || changed_f(n.width, size.x) || changed_f(n.height, auto_h) || !n.has_layout)
      {
        n.pos_x = pos.x;
        n.pos_y = pos.y;
        n.width = size.x;
        n.height = auto_h;
        n.has_layout = true;
        layout_dirty_ = true;
      }

      ImGui::End();
      ImGui::PopStyleColor(folder_theme_count);

      if(!note_window_open)
      {
        n.hidden = true;
        if(is_editing_this)
        {
          normalize_input_text_buffer(markdown_text_);
          save_state();
          editing_mode_ = false;
        }
        save_index();
      }
    }

    if(box_apply_pending)
    {
      box_apply_pending = false;
      const ImVec2 rmin(std::min(box_apply_start.x, box_apply_end.x), std::min(box_apply_start.y, box_apply_end.y));
      const ImVec2 rmax(std::max(box_apply_start.x, box_apply_end.x), std::max(box_apply_start.y, box_apply_end.y));
      const bool tiny = (std::fabs(rmax.x - rmin.x) < 4.0f) && (std::fabs(rmax.y - rmin.y) < 4.0f);
      selected_note_indices.clear();
      selected_stroke_indices.clear();

      if(!tiny)
      {
        auto intersects = [&](ImVec2 a0, ImVec2 a1) {
          if(a1.x < rmin.x || a0.x > rmax.x) return false;
          if(a1.y < rmin.y || a0.y > rmax.y) return false;
          return true;
        };

        for(const auto &nr : note_rects)
        {
          if(intersects(nr.min, nr.max)) selected_note_indices.insert(nr.idx);
        }

        auto &folder_strokes_sel = g_folder_drawings[f.name];
        for(int si = 0; si < (int)folder_strokes_sel.size(); ++si)
        {
          const auto &s = folder_strokes_sel[(size_t)si];
          bool inside = false;
          for(const ImVec2 &pn : s.points)
          {
            const ImVec2 ps(bg_p0.x + pn.x, bg_p0.y + pn.y);
            if(ps.x >= rmin.x && ps.x <= rmax.x && ps.y >= rmin.y && ps.y <= rmax.y)
            {
              inside = true;
              break;
            }
          }
          if(inside) selected_stroke_indices.insert(si);
        }
      }
    }

    if(request_delete_selected_ && !editing_mode_)
    {
      const bool has_note_sel = !selected_note_indices.empty();
      const bool has_stroke_sel = !selected_stroke_indices.empty();
      if(has_note_sel || has_stroke_sel)
      {
        push_selection_snapshot();
        if(has_note_sel)
        {
          std::vector<int> nd(selected_note_indices.begin(), selected_note_indices.end());
          std::sort(nd.begin(), nd.end(), std::greater<int>());
          for(int idx : nd)
          {
            if(idx < 0 || idx >= (int)f.notes.size()) continue;
            queue_pending_delete_path(f.notes[(size_t)idx].path);
            f.notes.erase(f.notes.begin() + idx);
          }
          if(f.notes.empty())
            active_note_idx_ = -1;
          else
            active_note_idx_ = std::max(0, std::min(active_note_idx_, (int)f.notes.size() - 1));
          load_note_content_for_active();
          save_index();
          layout_dirty_ = true;
        }
        if(has_stroke_sel)
        {
          std::vector<int> sd(selected_stroke_indices.begin(), selected_stroke_indices.end());
          std::sort(sd.begin(), sd.end(), std::greater<int>());
          for(int si : sd)
          {
            if(si < 0 || si >= (int)folder_strokes.size()) continue;
            folder_strokes.erase(folder_strokes.begin() + si);
          }
          g_drawings_dirty = true;
        }
        selected_note_indices.clear();
        selected_stroke_indices.clear();
      }
      request_delete_selected_ = false;
    }

    if(pending_group_mover >= 0 &&
       (std::fabs(pending_group_delta.x) > 0.01f || std::fabs(pending_group_delta.y) > 0.01f))
    {
      push_selection_snapshot();
      for(int idx : selected_note_indices)
      {
        if(idx == pending_group_mover) continue;
        if(idx < 0 || idx >= (int)f.notes.size()) continue;
        NoteMeta &sn = f.notes[(size_t)idx];
        if(sn.hidden) continue;
        sn.pos_x += pending_group_delta.x;
        sn.pos_y += pending_group_delta.y;
        std::string sn_uid = sn.path;
        for(char &c : sn_uid)
        {
          if(c == '#') c = '_';
        }
        const std::string sid = sn.title + "###FolderNote_" + sn_uid;
        ImGui::SetWindowPos(sid.c_str(), ImVec2(sn.pos_x, sn.pos_y), ImGuiCond_Always);
        layout_dirty_ = true;
      }

      if(!selected_stroke_indices.empty())
      {
        push_selection_snapshot();
        auto &folder_strokes_move = g_folder_drawings[f.name];
        for(int si : selected_stroke_indices)
        {
          if(si < 0 || si >= (int)folder_strokes_move.size()) continue;
          auto &s = folder_strokes_move[(size_t)si];
          for(ImVec2 &pn : s.points)
          {
            pn.x += pending_group_delta.x;
            pn.y += pending_group_delta.y;
          }
        }
        g_drawings_dirty = true;
      }
    }

    if(!topbar_tooltip_text.empty())
    {
      ImDrawList *fg = ImGui::GetForegroundDrawList();
      const ImVec2 m = ImGui::GetMousePos();
      const ImVec2 pad(8.0f, 5.0f);
      const ImVec2 text_sz = ImGui::CalcTextSize(topbar_tooltip_text.c_str());
      const ImVec2 pmin(m.x + 14.0f, m.y + 16.0f);
      const ImVec2 pmax(pmin.x + text_sz.x + pad.x * 2.0f, pmin.y + text_sz.y + pad.y * 2.0f);
      fg->AddRectFilled(pmin, pmax, ImGui::GetColorU32(ImVec4(0.10f, 0.10f, 0.12f, 0.96f)), 5.0f);
      fg->AddRect(pmin, pmax, ImGui::GetColorU32(ImVec4(0.55f, 0.57f, 0.62f, 0.95f)), 5.0f, 0, 1.0f);
      fg->AddText(ImVec2(pmin.x + pad.x, pmin.y + pad.y), ImGui::GetColorU32(ImVec4(0.96f, 0.96f, 0.98f, 1.0f)), topbar_tooltip_text.c_str());
    }

    if(editing_mode_ && request_exit_edit_mode_)
    {
      normalize_input_text_buffer(markdown_text_);
      save_state();
      editing_mode_ = false;
      request_exit_edit_mode_ = false;
    }

    if(layout_dirty_ && !ImGui::IsAnyMouseDown())
    {
      save_index();
      layout_dirty_ = false;
    }
    if(g_drawings_dirty && !ImGui::IsAnyMouseDown()) save_drawings_state();
    if(g_clipboard_dirty && !ImGui::IsAnyMouseDown()) save_note_clipboard();
    return;
  }
  request_undo_draw_ = false;
  request_redo_draw_ = false;
  request_delete_selected_ = false;

  // --- Single window: "Note" (preview + edit overlay) ---
  ensure_default_index();
  if(!has_active_note())
  {
    editing_mode_ = false;
    note_title_ = "Note";
    state_file_path_.clear();
    if(request_rename_selected_) request_rename_selected_ = false;

    ImGui::SetNextWindowSize(ImVec2(520.0f, 180.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "Note###NoteWindowEmpty",
        nullptr,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("This folder has no notes.");
    ImGui::TextUnformatted("Right click in Explorer or Notes background to create one.");
    ImGui::End();

    if(g_drawings_dirty && !ImGui::IsAnyMouseDown()) save_drawings_state();
    if(g_clipboard_dirty && !ImGui::IsAnyMouseDown()) save_note_clipboard();
    return;
  }
  NoteMeta &active_note = folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_];
  static float note_window_height = 360.0f;
  auto compute_edit_window_height = [&]() -> float {
    const ImGuiStyle &st = ImGui::GetStyle();
    const float title_bar_h = ImGui::GetFontSize() + st.FramePadding.y * 2.0f;
    const int line_count = 1 + (int)std::count(markdown_text_.begin(), markdown_text_.end(), '\n');
    const float text_h = line_count * ImGui::GetTextLineHeightWithSpacing();
    const float input_h = text_h + st.FramePadding.y * 2.0f + 10.0f;
    return std::max(140.0f, title_bar_h + st.WindowPadding.y * 2.0f + input_h);
  };

  if(editing_mode_) note_window_height = compute_edit_window_height();

  if(active_note.has_layout)
  {
    ImGui::SetNextWindowPos(ImVec2(active_note.pos_x, active_note.pos_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, active_note.width), std::max(140.0f, active_note.height)), ImGuiCond_FirstUseEver);
  }
  else
  {
    ImGui::SetNextWindowSize(ImVec2(520.0f, note_window_height), ImGuiCond_FirstUseEver);
  }

  ImGui::SetNextWindowSizeConstraints(
      ImVec2(320.0f, note_window_height),
      ImVec2(FLT_MAX, note_window_height));

  std::string note_window_label = note_title_ + "###NoteWindow";
  const FolderMeta &active_folder = folders_[(size_t)active_folder_idx_];
  const int active_folder_theme_count = push_folder_imgui_theme(
      make_note_theme(
          active_folder.use_custom_color,
          active_folder.color_r,
          active_folder.color_g,
          active_folder.color_b,
          ImGui::GetStyle()),
      ImGui::GetStyle());
  ImGui::Begin(
      note_window_label.c_str(),
      nullptr,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

  // Right click on title bar to rename the note window title.
  static bool open_rename_popup = false;
  static char rename_buf[256] = {};
  const ImVec2 win_pos = ImGui::GetWindowPos();
  const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
  const ImVec2 mouse_pos = ImGui::GetMousePos();
  const bool mouse_on_title =
      mouse_pos.x >= win_pos.x &&
      mouse_pos.x <= (win_pos.x + ImGui::GetWindowWidth()) &&
      mouse_pos.y >= win_pos.y &&
      mouse_pos.y <= (win_pos.y + title_bar_h);
  if(mouse_on_title && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", note_title_.c_str());
    ImGui::SetWindowCollapsed(note_window_label.c_str(), false, ImGuiCond_Always);
    open_rename_popup = true;
  }
  if(mouse_on_title && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
  {
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", note_title_.c_str());
    open_rename_popup = true;
  }
  if(request_rename_selected_)
  {
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", note_title_.c_str());
    open_rename_popup = true;
    request_rename_selected_ = false;
  }
  if(open_rename_popup)
  {
    ImGui::OpenPopup("Rename Note");
    open_rename_popup = false;
  }
  if(ImGui::BeginPopup("Rename Note"))
  {
    ImGui::TextUnformatted("Nom de la finestra:");
    ImGui::SetNextItemWidth(260.0f);
    if(ImGui::InputText("##rename_note_title", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      normalize_input_text_buffer(markdown_text_);
      rename_note_storage_for_title(rename_buf);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  static bool show_palette = false;
  static bool refocus_editor = false;
  static MdFormatState fmt;
  static MdEditorUserData ud{&markdown_text_, &fmt};
  ud.text = &markdown_text_;

  if(!editing_mode_)
  {
    // Preview mode (interactive)
    const float start_y = ImGui::GetCursorPosY();
    const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f);
    MarkdownView::set_render_width(preview_w);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    const bool task_changed = render_preview_with_task_checkboxes(markdown_text_);
    ImGui::PopTextWrapPos();
    if(task_changed) save_state();
    (void)start_y;

    // Enter edit mode only on double click (single click does nothing)
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      editing_mode_ = true;
      show_palette = false;
      // Next frame, focus the editor widget
      refocus_editor = true;
    }
  }
  else
  {
    // Plain text editor mode
    ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput |
        ImGuiInputTextFlags_CallbackResize |
        ImGuiInputTextFlags_CallbackEdit |
        ImGuiInputTextFlags_CallbackAlways;
    if(refocus_editor)
    {
      ImGui::SetKeyboardFocusHere();
      refocus_editor = false;
    }

    const std::string before_edit = markdown_text_;
    const bool text_changed = ImGui::InputTextMultiline(
        "##md",
        markdown_text_.data(),
        markdown_text_.capacity() + 1,
        ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
        flags,
        [](ImGuiInputTextCallbackData *data) -> int {
          auto *ud = static_cast<MdEditorUserData *>(data->UserData);
          if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
          {
            ud->text->resize((size_t)data->BufTextLen);
            data->Buf = ud->text->data();
            return 0;
          }
          data->UserData = ud->fmt; // md_editor_cb expects MdFormatState*
          return md_editor_cb(data);
        },
        &ud);
    normalize_input_text_buffer(markdown_text_);
    if(text_changed)
    {
      if(should_push_word_granular_undo(before_edit, markdown_text_, fmt))
        push_undo_snapshot_from(before_edit);
      save_state();
    }
    if(request_undo_edit_)
    {
      apply_undo_snapshot();
      request_undo_edit_ = false;
      request_redo_edit_ = false;
    }
    if(request_redo_edit_)
    {
      apply_redo_snapshot();
      request_redo_edit_ = false;
    }

    // After the widget: show popup if selection is non-empty and editor is focused/active
    const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImGuiIO &io = ImGui::GetIO();
    if(editor_hovered && io.MouseClickedCount[ImGuiMouseButton_Left] >= 3)
    {
      const auto [ls, le] = line_bounds_from_cursor(markdown_text_, fmt.cursor_pos);
      fmt.pending_select_range = true;
      fmt.pending_sel_start = ls;
      fmt.pending_sel_end = le;
      fmt.selection_anchor = ls;
    }
    if(editor_hovered && io.KeyCtrl && io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      fmt.pending_select_range = true;
      fmt.pending_sel_start = fmt.selection_anchor;
      fmt.pending_sel_end = fmt.cursor_pos;
    }

    const int a = fmt.sel_start, b = fmt.sel_end;
    const bool has_selection = (a != b);
    const int sel_min = (a < b) ? a : b;
    const int sel_max = (a < b) ? b : a;
    static int anchor_sel_start = 0;
    static int anchor_sel_end = 0;

    // ---- Clickable floating formatting palette (tooltip-like) ----
    static ImVec2 palette_pos(0, 0);
    static bool palette_just_opened = false;

    if(has_selection)
    {
      anchor_sel_start = sel_min;
      anchor_sel_end = sel_max;
    }

    const bool has_anchor_selection = (anchor_sel_start != anchor_sel_end);

    // Open palette only with right-click while editing and text is selected.
    if(editor_hovered &&
       (has_selection || has_anchor_selection) &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      palette_pos = ImGui::GetMousePos();
      show_palette = true;
      palette_just_opened = true;
    }
    bool palette_hovered = false;
    if(show_palette && has_anchor_selection)
    {
      ImGui::SetNextWindowPos(palette_pos, ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.95f);
      if(palette_just_opened) ImGui::SetNextWindowFocus();

      ImGuiWindowFlags pal_flags =
          ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNavFocus |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking; // NOTE: no Tooltip flag (Tooltip => NoInputs)

      ImGui::Begin("##md_format_palette", nullptr, pal_flags);
      const bool pal_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      palette_hovered = pal_hovered;

      bool applied = false;

      if(ImGui::Button("Italic"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Bold"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Strike"))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button("Note"))
      {
        push_undo_snapshot();
        apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
        applied = true;
      }

      ImGui::Separator();

      ImGui::ColorEdit3("Color", (float *)&fmt.color, ImGuiColorEditFlags_NoInputs);
      ImGui::SameLine();
      if(ImGui::Button("Apply"))
      {
        push_undo_snapshot();
        const std::string hex = rgba_to_hex(fmt.color);
        apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, hex);
        applied = true;
      }

      ImGui::End();

      if(applied)
      {
        normalize_input_text_buffer(markdown_text_);
        save_state();
        fmt.sel_start = anchor_sel_start;
        fmt.sel_end = anchor_sel_end;
      }
    }

    // Hide when clicking outside tooltip window (or if selection is gone).
    const bool any_popup_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool clicked_outside_palette =
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
        !palette_hovered &&
        !any_popup_open &&
        !palette_just_opened;
    if(show_palette && (!has_anchor_selection || clicked_outside_palette))
    {
      show_palette = false;
      anchor_sel_start = 0;
      anchor_sel_end = 0;
    }
    palette_just_opened = false;
  }

  // Exit edit mode only with Esc.
  if(editing_mode_ && request_exit_edit_mode_)
  {
    normalize_input_text_buffer(markdown_text_);
    save_state();
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    show_palette = false;
    refocus_editor = false;
  }

  if(editing_mode_)
    note_window_height = compute_edit_window_height();
  else
    note_window_height = std::max(140.0f, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);

  {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    auto changed_f = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
    if(changed_f(active_note.pos_x, pos.x) || changed_f(active_note.pos_y, pos.y) ||
       changed_f(active_note.width, size.x) || changed_f(active_note.height, note_window_height) ||
       !active_note.has_layout)
    {
      active_note.pos_x = pos.x;
      active_note.pos_y = pos.y;
      active_note.width = size.x;
      active_note.height = note_window_height;
      active_note.has_layout = true;
      layout_dirty_ = true;
    }
  }

  ImGui::End();
  ImGui::PopStyleColor(active_folder_theme_count);

  if(layout_dirty_ && !ImGui::IsAnyMouseDown())
  {
    save_index();
    layout_dirty_ = false;
  }
  if(g_drawings_dirty && !ImGui::IsAnyMouseDown()) save_drawings_state();
  if(g_clipboard_dirty && !ImGui::IsAnyMouseDown()) save_note_clipboard();
}

void App::frame_end()
{
  ImGui::Render();

  int display_w = 0, display_h = 0;
  SDL_GL_GetDrawableSize(window_, &display_w, &display_h);

  glViewport(0, 0, display_w, display_h);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(window_);
}
