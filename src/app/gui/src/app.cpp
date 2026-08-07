#include "app.hpp"
#include "atomic_file.hpp"
#ifdef IMGUI_ENABLE_FREETYPE
#include "imgui_freetype.h"
#endif
#include "demo_note_content.hpp"
#include "lang.hpp"
#include "layout_profile_state.hpp"
#include "log.hpp"
#if USE_PORTABLE_PATHS
#include "note_project.hpp"
#endif
#include "project_paths.hpp"
#include "markdown_sections.hpp"
#include "markdown_support.hpp"
#include "markdown_view.hpp"
#include "markdown_widgets.hpp"
#include "note_ui.hpp"
#include "note_clipboard_state.hpp"
#include "note_index.hpp"
#include "string_utils.hpp"
#include "sync_coordinator.hpp"
#include "tiny_json.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <numeric>
#include <utility>
#include <random>
#include <cstdlib>

#include <SDL.h>
#include <SDL_opengl.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif
#if defined(__APPLE__)
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

using MarkdownSupport::apply_color_wrap_string;
using MarkdownSupport::apply_note_quote;
using MarkdownSupport::apply_preview_state_snapshot;
using MarkdownSupport::apply_wrap_string;
using MarkdownSupport::capture_preview_state_snapshot;
using MarkdownSupport::insert_checklist_item_at_cursor;
using MarkdownSupport::insert_markdown_table_at_cursor;
using MarkdownSupport::line_bounds_from_cursor;
using MarkdownSupport::md_editor_cb;
using MarkdownSupport::MdEditorUserData;
using MarkdownSupport::MdFormatState;
using MarkdownSupport::normalize_input_text_buffer;
using MarkdownSupport::render_preview_with_task_checkboxes;
using MarkdownSupport::render_preview_with_task_checkboxes_ex;
using MarkdownSupport::request_preview_heading;
using MarkdownSupport::request_preview_source_offset;
using MarkdownSupport::rgba_to_hex;

struct ExplorerNoteDragPayload
{
  ImVec2 indices{};
  char reference[512]{};
};

static ImVec2 nonzero_invisible_button_size(float w, float h)
{
  return ImVec2(std::max(1.0f, w), std::max(1.0f, h));
}

static bool split_command_words(std::string_view command, std::vector<std::string> &words)
{
  words.clear();
  std::string current;
  char quote = '\0';
  bool escaped = false;
  for(const char c : command)
  {
    if(escaped)
    {
      current.push_back(c);
      escaped = false;
    }
    else if(c == '\\' && quote != '\'')
      escaped = true;
    else if(quote != '\0')
    {
      if(c == quote)
        quote = '\0';
      else
        current.push_back(c);
    }
    else if(c == '\'' || c == '"')
      quote = c;
    else if(std::isspace(static_cast<unsigned char>(c)))
    {
      if(!current.empty())
      {
        words.push_back(std::move(current));
        current.clear();
      }
    }
    else
      current.push_back(c);
  }
  if(escaped) current.push_back('\\');
  if(quote != '\0')
  {
    words.clear();
    return false;
  }
  if(!current.empty()) words.push_back(std::move(current));
  return true;
}

static bool parse_hex_color(std::string_view text, int &r, int &g, int &b)
{
  if(text.size() != 7U || text.front() != '#') return false;
  const auto hex = [](char c) -> int {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  const int values[] = {hex(text[1]), hex(text[2]), hex(text[3]),
                        hex(text[4]), hex(text[5]), hex(text[6])};
  for(const int value : values)
    if(value < 0) return false;
  r = values[0] * 16 + values[1];
  g = values[2] * 16 + values[3];
  b = values[4] * 16 + values[5];
  return true;
}

static constexpr char kToolbarUiWidgetChooserPopup[] = "##tb_ui_widgets_popup";
static constexpr char kEditorActionsUiWidgetChooserPopup[] =
    "Insert UI Block###editor_actions_ui_widget_chooser_popup";

static bool cursor_is_inside_ui_block(const std::string &text, int cursor)
{
  if(text.empty() || cursor <= 0) return false;

  const std::size_t cursor_pos = static_cast<std::size_t>(
      std::min(cursor, static_cast<int>(text.size())));
  bool inside = false;
  std::size_t pos = 0;
  while(pos < text.size())
  {
    const std::size_t newline = text.find('\n', pos);
    const std::size_t line_end = newline == std::string::npos ? text.size() : newline;
    std::string_view line(text.data() + pos, line_end - pos);
    StringUtils::trim(line);
    if(line == "```ui")
    {
      if(!inside && pos <= cursor_pos && cursor_pos <= line_end) return true;
      inside = true;
    }
    else if(line == "```" && inside)
    {
      if(cursor_pos <= line_end) return true;
      inside = false;
    }
    if(newline == std::string::npos) break;
    pos = newline + 1;
  }
  return inside && cursor_pos >= pos;
}

static std::string trim_ui_label(std::string text)
{
  const std::size_t first = text.find_first_not_of(" \t\n\r");
  if(first == std::string::npos) return {};
  const std::size_t last = text.find_last_not_of(" \t\n\r");
  return text.substr(first, last - first + 1);
}

static std::string escape_ui_string(const std::string &text)
{
  std::string out;
  out.reserve(text.size() + 8);
  for(const char c : text)
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
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

static std::string make_ui_identifier(const std::string &label)
{
  std::string out;
  out.reserve(label.size() + 4);
  for(const char c : label)
  {
    const bool is_lower = c >= 'a' && c <= 'z';
    const bool is_upper = c >= 'A' && c <= 'Z';
    const bool is_digit = c >= '0' && c <= '9';
    if(is_lower || is_upper || is_digit)
      out.push_back(is_upper ? static_cast<char>(c - 'A' + 'a') : c);
    else if((c == ' ' || c == '-' || c == '_') && !out.empty() && out.back() != '_')
      out.push_back('_');
  }
  while(!out.empty() && out.back() == '_') out.pop_back();
  if(out.empty()) out = "inventory";
  if(out.front() >= '0' && out.front() <= '9') out.insert(out.begin(), 'i');
  return out;
}

static std::string build_inventory_snippet(const char *raw_title, int rows, int cols)
{
  const int safe_rows = std::max(1, rows);
  const int safe_cols = std::max(1, cols);
  std::string label = trim_ui_label(raw_title ? std::string(raw_title) : std::string());
  if(label.empty()) label = "Inventory";
  const std::string variable_name = make_ui_identifier(label) + "_data";
  const int width = std::max(220, safe_cols * 58 + 12);
  std::ostringstream out;
  out << variable_name << "({\n";
  out << "  items:[\n";
  out << "    {name:\"Potion\", image:\"potion.png\", tooltip:\"Consumable item\", quantity:3, color:\"#57A7FF\"}";
  if(safe_rows * safe_cols > 1)
    out << ",\n    {tooltip:\"Disabled example cell\", color:\"#FFB347\", enabled:false}\n";
  else
    out << "\n";
  out << "  ]\n";
  out << "})\n";
  out << "inventory(" << variable_name << ", \"" << escape_ui_string(label) << "\", " << width << ", " << safe_rows << ", " << safe_cols << ")\n";
  out << "__CURSOR__";
  return out.str();
}

template <typename InsertSnippet, typename CreateGlobals>
static bool render_ui_widget_chooser(
    const char *popup_id,
    bool modal,
    bool close_requested,
    InsertSnippet &&insert_snippet,
    CreateGlobals &&create_globals)
{
  static char inventory_builder_title[128] = "Inventory";
  static int inventory_builder_rows = 2;
  static int inventory_builder_cols = 2;
  bool open_inventory_builder = false;
  const bool chooser_open = modal
                                ? ImGui::BeginPopupModal(
                                      popup_id,
                                      nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoSavedSettings)
                                : ImGui::BeginPopup(popup_id);
  if(chooser_open)
  {
    if(close_requested)
    {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return false;
    }
    ImGui::TextDisabled("UI Blocks");
    if(ImGui::MenuItem("Full UI example"))
    {
      insert_snippet(R"MD(count(10)
name("Sauron")
enabled(true)
mode("Home")
tags(["daily", "important"])
total(count*2)
text("Count: ") int(count, "Count", 90, true)
text(" Name: ") text(name, "Name", 150, "Edit the name")
checkbox(enabled, "Enabled")
enum(mode, "Mode", 140, ["Home", "Work", "Ideas"])
multicheck(tags, "Tags", 180, ["daily", "important", "later"])
if(enabled){
  text("Visible total: ") text(total)
}
button("Reset count", 110, count=0)
__CURSOR__)MD",
                     true);
      ImGui::CloseCurrentPopup();
    }
    if(modal) ImGui::SetItemDefaultFocus();
    ImGui::Separator();
    ImGui::TextDisabled("Variables");
    if(ImGui::MenuItem("Variable example"))
    {
      insert_snippet("valueA(10)\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Computed variable example"))
    {
      insert_snippet("valueA(10)\nvalueB(valueA+5)\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Global variable"))
    {
      create_globals();
      ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Widgets");
    if(ImGui::MenuItem("Text output"))
    {
      insert_snippet("valueA(10)\ntext(\"Value: \") text(valueA)\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Text input"))
    {
      insert_snippet("name(\"Sauron\")\ntext(name, \"Name\", 150, \"Edit the name\")\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Integer input"))
    {
      insert_snippet("count(10)\nint(count, \"Count\", 90, true)\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Slider"))
    {
      insert_snippet("volume(50)\nslider(volume, \"Volume\", 160, 0, 100)\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Checkbox"))
    {
      insert_snippet("enabled(true)\ncheckbox(enabled, \"Enabled\")\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Enum"))
    {
      insert_snippet("mode(\"Home\")\nenum(mode, \"Mode\", 140, [\"Home\", \"Work\", \"Ideas\"])\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Multicheck"))
    {
      insert_snippet("tags([\"daily\"])\nmulticheck(tags, \"Tags\", 180, [\"daily\", \"important\", \"later\"])\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("List"))
    {
      insert_snippet(R"MD(items([
  {name:"Sword", tooltip:"Basic weapon"},
  {name:"Shield", tooltip:"Blocks attacks"},
  {name:"Potion", tooltip:"Restores health"}
])
list(items, "Inventory items", 220, true)
__CURSOR__)MD",
                     true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Inventory..."))
    {
      open_inventory_builder = true;
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Button action"))
    {
      insert_snippet("count(10)\nbutton(\"Reset count\", 110, count=0)\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Command button"))
    {
      insert_snippet("button(\"List files\", 110, command(\"ls\"))\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    if(ImGui::MenuItem("Conditional if block"))
    {
      insert_snippet("enabled(true)\ncheckbox(enabled, \"Enabled\")\nif(enabled){\n  text(\"Visible when enabled\")\n}\n__CURSOR__\n", true);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if(open_inventory_builder) ImGui::OpenPopup("##tb_inventory_builder_popup");
  if(ImGui::BeginPopupModal("##tb_inventory_builder_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextDisabled("%s", Lang::t("Insert Inventory Widget"));
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText(Lang::t("Title"), inventory_builder_title, sizeof(inventory_builder_title));
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt(Lang::t("Rows"), &inventory_builder_rows);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt(Lang::t("Cols"), &inventory_builder_cols);
    inventory_builder_rows = std::max(1, inventory_builder_rows);
    inventory_builder_cols = std::max(1, inventory_builder_cols);
    ImGui::TextDisabled("Slots: %d", inventory_builder_rows * inventory_builder_cols);
    if(ImGui::Button(Lang::t("Insert")))
    {
      insert_snippet(build_inventory_snippet(inventory_builder_title, inventory_builder_rows, inventory_builder_cols), true);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button(Lang::t("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  return chooser_open;
}

static ImGuiWindow *input_text_multiline_child(ImGuiID input_id)
{
  if(GImGui == nullptr) return nullptr;

  // InputTextMultiline's child window has a generated window ID; ChildId retains
  // the input widget ID after the child has been ended.
  ImGuiWindow *const parent = ImGui::GetCurrentWindowRead();
  for(ImGuiWindow *child : parent->DC.ChildWindows)
  {
    if(child->ChildId == input_id) return child;
  }
  return nullptr;
}

static int line_index_at_offset(std::string_view text, int offset)
{
  const int target = std::clamp(offset, 0, static_cast<int>(text.size()));
  return static_cast<int>(std::count(text.begin(), text.begin() + target, '\n'));
}

static bool scroll_input_text_multiline_to_offset(
    ImGuiID input_id, std::string_view text, int offset)
{
  ImGuiWindow *const child = input_text_multiline_child(input_id);
  if(child == nullptr) return false;

  const int target = std::clamp(offset, 0, static_cast<int>(text.size()));
  const std::size_t target_offset = static_cast<std::size_t>(target);
  const std::size_t line_break_before =
      target_offset == 0 ? std::string_view::npos : text.rfind('\n', target_offset - 1);
  const std::size_t line_start =
      line_break_before == std::string_view::npos ? 0 : line_break_before + 1;
  const float line_y = child->DC.CursorStartPos.y - child->Pos.y +
                       ImGui::GetStyle().FramePadding.y +
                       static_cast<float>(line_index_at_offset(text, target)) * ImGui::GetFontSize();
  const float line_x = child->DC.CursorStartPos.x - child->Pos.x +
                       ImGui::GetStyle().FramePadding.x +
                       ImGui::CalcTextSize(
                           text.data() + line_start, text.data() + target_offset)
                           .x;
  ImGui::SetScrollFromPosX(child, line_x, 0.5f);
  ImGui::SetScrollFromPosY(child, line_y, 0.5f);
  return true;
}

static void draw_input_text_multiline_match_indicator(
    ImGuiID input_id, std::string_view text, int offset, int length)
{
  ImGuiWindow *const child = input_text_multiline_child(input_id);
  if(child == nullptr || text.empty() || length <= 0) return;

  const int text_length = static_cast<int>(text.size());
  const int match_start = std::clamp(offset, 0, text_length);
  const int match_length = std::clamp(length, 0, text_length - match_start);
  const int match_end = match_start + match_length;
  if(match_start == match_end) return;

  const std::size_t start = static_cast<std::size_t>(match_start);
  const std::size_t line_break_before =
      start == 0 ? std::string_view::npos : text.rfind('\n', start - 1);
  const std::size_t line_start =
      line_break_before == std::string_view::npos ? 0 : line_break_before + 1;
  const std::size_t line_end = text.find_first_of("\r\n", start);
  const std::size_t first_segment_end = std::min(
      static_cast<std::size_t>(match_end),
      line_end == std::string_view::npos ? text.size() : line_end);

  const char *const text_begin = text.data();
  const char *const match_begin = text_begin + match_start;
  const char *const segment_end = text_begin + first_segment_end;
  const float prefix_width =
      ImGui::CalcTextSize(text_begin + line_start, match_begin).x;
  const float segment_width =
      std::max(1.0f, ImGui::CalcTextSize(match_begin, segment_end).x);

  const ImGuiStyle &style = ImGui::GetStyle();
  const float line_height = ImGui::GetFontSize();
  // CursorStartPos already includes the child scroll offset. Match
  // InputTextMultiline's padded text origin without applying it again.
  const ImVec2 text_origin(
      child->DC.CursorStartPos.x + style.FramePadding.x,
      child->DC.CursorStartPos.y + style.FramePadding.y);
  const float y0 = text_origin.y +
                   static_cast<float>(line_index_at_offset(text, match_start)) * line_height;
  const float x0 = text_origin.x + prefix_width;
  const float x1 = x0 + segment_width;
  const ImRect clip_rect = child->InnerClipRect;
  const float gutter_x1 = std::min(clip_rect.Min.x + 2.0f, clip_rect.Max.x);
  if(gutter_x1 <= clip_rect.Min.x) return;

  const ImU32 background = ImGui::GetColorU32(ImGuiCol_NavHighlight, 0.28f);
  const ImU32 accent = ImGui::GetColorU32(ImGuiCol_NavHighlight, 0.95f);
  const float underline_y = y0 + line_height - 1.0f;
  const float bracket_height = std::min(4.0f, line_height * 0.35f);
  child->DrawList->PushClipRect(clip_rect.Min, clip_rect.Max, true);
  // Keep the existing line marker while drawing a range-specific overlay.
  child->DrawList->AddRectFilled(
      ImVec2(clip_rect.Min.x, y0), ImVec2(gutter_x1, y0 + line_height), accent);
  child->DrawList->AddRectFilled(
      ImVec2(x0, y0), ImVec2(x1, y0 + line_height), background);
  child->DrawList->AddLine(ImVec2(x0, underline_y), ImVec2(x1, underline_y), accent, 1.5f);
  child->DrawList->AddLine(
      ImVec2(x0, underline_y), ImVec2(x0, underline_y - bracket_height), accent, 1.5f);
  child->DrawList->AddLine(
      ImVec2(x1, underline_y), ImVec2(x1, underline_y - bracket_height), accent, 1.5f);
  child->DrawList->PopClipRect();
}
using MarkdownSupport::set_all_preview_headers_open;
using MarkdownSupport::set_preview_document_path;
using MarkdownSupport::should_push_word_granular_undo;
using MarkdownSupport::summarize_preview_header_states;
using MarkdownSupport::word_bounds_from_double_click;

using StringUtils::clamp01f;
using StringUtils::sanitize_note_filename;

using NoteUi::clear_toolbar_icon_cache;
using NoteUi::folder_accent_color;
using NoteUi::get_toolbar_icon_size;
using NoteUi::get_toolbar_icon_texture;
using NoteUi::make_note_theme;
using NoteUi::mix_color;
using NoteUi::push_folder_imgui_theme;
using NoteUi::shaded_icon_button;
using NoteUi::with_alpha;

using TinyJson::find_matching;
using TinyJson::json_array_objects;
using TinyJson::json_escape;
using TinyJson::json_find_bool;
using TinyJson::json_find_float;
using TinyJson::json_find_int;
using TinyJson::json_find_string;
using TinyJson::json_unescape;

using Json = nlohmann::json;

namespace
{
constexpr Uint32 kProjectFilesChangedEvent = SDL_USEREVENT + 17;
constexpr Uint32 kGitSyncCompletedEvent = SDL_USEREVENT + 18;

Uint32 project_file_watch_timer_callback(Uint32 interval, void *)
{
  SDL_Event event{};
  event.type = kProjectFilesChangedEvent;
  SDL_PushEvent(&event);
  return interval;
}

std::string generate_uuid()
{
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dis(0, 0xFF);
  uint8_t bytes[16];
  for(auto &b : bytes) b = (uint8_t)dis(gen);
  bytes[6] = (bytes[6] & 0x0F) | 0x40;
  bytes[8] = (bytes[8] & 0x3F) | 0x80;
  char buf[37];
  snprintf(buf, sizeof(buf),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
           bytes[6], bytes[7], bytes[8], bytes[9],
           bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(buf);
}

std::string project_notes_top_level(const AppConfig &config)
{
  const std::filesystem::path relative =
      config.dataPath.lexically_normal().lexically_relative(config.projectRoot.lexically_normal());
  if(relative.empty()) return "notes";
  return (*relative.begin()).generic_string();
}

std::optional<std::filesystem::path> resolve_project_owned_path(
    const notepp::project_paths::ProjectPaths &paths,
    std::string_view stored_path,
    int schema_version,
    std::string_view expected_top_level)
{
  if(schema_version >= 2)
  {
    auto decoded = paths.decode(stored_path);
    if(!decoded) return std::nullopt;
    auto encoded = paths.encode(*decoded);
    if(!encoded) return std::nullopt;
    const std::string prefix = std::string(expected_top_level) + "/";
    if(*encoded != expected_top_level && !encoded->starts_with(prefix)) return std::nullopt;
    return *decoded;
  }

  auto migrated = paths.migrate_legacy(stored_path, expected_top_level);
  if(!migrated) return std::nullopt;
  return migrated->absolute_path;
}

std::optional<std::string> portable_project_path(
    const notepp::project_paths::ProjectPaths &paths,
    const std::filesystem::path &runtime_path,
    std::string_view expected_top_level)
{
  auto encoded = paths.encode(runtime_path);
  if(!encoded) return std::nullopt;
  const std::string prefix = std::string(expected_top_level) + "/";
  if(*encoded != expected_top_level && !encoded->starts_with(prefix)) return std::nullopt;
  return *encoded;
}

int clamp_to_range(int value, int lo, int hi)
{
  if(hi < lo) return lo;
  return std::max(lo, std::min(value, hi));
}

int rect_intersection_area(const SDL_Rect &a, const SDL_Rect &b)
{
  const int x0 = std::max(a.x, b.x);
  const int y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.w, b.x + b.w);
  const int y1 = std::min(a.y + a.h, b.y + b.h);
  if(x1 <= x0 || y1 <= y0) return 0;
  return (x1 - x0) * (y1 - y0);
}

bool get_display_bounds(int display_index, SDL_Rect &bounds)
{
  if(SDL_GetDisplayUsableBounds(display_index, &bounds) == 0) return true;
  return SDL_GetDisplayBounds(display_index, &bounds) == 0;
}

bool current_video_driver_is_wayland()
{
  const char *driver = SDL_GetCurrentVideoDriver();
  return driver != nullptr && std::strcmp(driver, "wayland") == 0;
}

int display_index_for_window(SDL_Window *window)
{
  if(window == nullptr) return 0;

  const int display_index = SDL_GetWindowDisplayIndex(window);
  if(display_index >= 0) return display_index;

  const int display_count = SDL_GetNumVideoDisplays();
  return display_count > 0 ? 0 : -1;
}

int display_index_for_point(int x, int y)
{
  const int display_count = SDL_GetNumVideoDisplays();
  for(int di = 0; di < display_count; ++di)
  {
    SDL_Rect bounds{};
    if(SDL_GetDisplayBounds(di, &bounds) != 0) continue;
    if(x >= bounds.x && x < bounds.x + bounds.w &&
       y >= bounds.y && y < bounds.y + bounds.h)
      return di;
  }
  return -1;
}

int best_display_index_for_rect(const SDL_Rect &rect)
{
  const int display_count = SDL_GetNumVideoDisplays();
  int best_index = -1;
  int best_area = -1;

  for(int di = 0; di < display_count; ++di)
  {
    SDL_Rect bounds{};
    if(!get_display_bounds(di, bounds)) continue;

    const int area = rect_intersection_area(rect, bounds);
    if(best_index < 0 || area > best_area)
    {
      best_index = di;
      best_area = area;
    }
  }

  return best_index;
}

void sanitize_window_rect_for_displays(int &x, int &y, int &w, int &h)
{
  static constexpr int kMinWindowW = 320;
  static constexpr int kMinWindowH = 200;

  w = std::max(kMinWindowW, w);
  h = std::max(kMinWindowH, h);

  const int display_count = SDL_GetNumVideoDisplays();
  if(display_count <= 0) return;

  const SDL_Rect proposed{x, y, w, h};
  SDL_Rect best_bounds{};
  int best_area = -1;
  bool have_bounds = false;

  for(int di = 0; di < display_count; ++di)
  {
    SDL_Rect bounds{};
    if(!get_display_bounds(di, bounds)) continue;

    const int area = rect_intersection_area(proposed, bounds);
    if(!have_bounds || area > best_area)
    {
      best_bounds = bounds;
      best_area = area;
      have_bounds = true;
    }
  }

  if(!have_bounds) return;

  w = std::min(w, std::max(kMinWindowW, best_bounds.w));
  h = std::min(h, std::max(kMinWindowH, best_bounds.h));
  x = clamp_to_range(x, best_bounds.x, best_bounds.x + best_bounds.w - w);
  y = clamp_to_range(y, best_bounds.y, best_bounds.y + best_bounds.h - h);
}

void apply_borderless_maximized_window(SDL_Window *window)
{
  if(window == nullptr) return;

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
    SDL_SetWindowFullscreen(window, 0);

  if(current_video_driver_is_wayland())
  {
    SDL_MaximizeWindow(window);
    return;
  }

  const int display_index = display_index_for_window(window);
  SDL_Rect bounds{};
  if(display_index < 0 || !get_display_bounds(display_index, bounds))
  {
    SDL_MaximizeWindow(window);
    return;
  }

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0)
    SDL_RestoreWindow(window);

  SDL_SetWindowResizable(window, SDL_TRUE);
  SDL_SetWindowBordered(window, SDL_FALSE);
  SDL_SetWindowMinimumSize(window, 320, 200);
  SDL_SetWindowMaximumSize(window, 0, 0);

  SDL_SetWindowPosition(window, bounds.x, bounds.y);
  SDL_SetWindowSize(window, bounds.w, bounds.h);
  SDL_SetWindowPosition(window, bounds.x, bounds.y);
}

void apply_borderless_maximized_window(SDL_Window *window, int preferred_x, int preferred_y, int preferred_w, int preferred_h)
{
  if(window == nullptr) return;

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
    SDL_SetWindowFullscreen(window, 0);

  if(current_video_driver_is_wayland())
  {
    SDL_MaximizeWindow(window);
    return;
  }

  SDL_Rect preferred{preferred_x, preferred_y, std::max(1, preferred_w), std::max(1, preferred_h)};
  int display_index = best_display_index_for_rect(preferred);
  if(display_index < 0) display_index = display_index_for_window(window);

  SDL_Rect bounds{};
  if(display_index < 0 || !get_display_bounds(display_index, bounds))
  {
    apply_borderless_maximized_window(window);
    return;
  }

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0)
    SDL_RestoreWindow(window);

  SDL_SetWindowResizable(window, SDL_TRUE);
  SDL_SetWindowBordered(window, SDL_FALSE);
  SDL_SetWindowMinimumSize(window, 320, 200);
  SDL_SetWindowMaximumSize(window, 0, 0);

  SDL_SetWindowPosition(window, bounds.x, bounds.y);
  SDL_SetWindowSize(window, bounds.w, bounds.h);
  SDL_SetWindowPosition(window, bounds.x, bounds.y);
}

void apply_borderless_window_rect(SDL_Window *window, int x, int y, int w, int h)
{
  if(window == nullptr) return;

  sanitize_window_rect_for_displays(x, y, w, h);

  if((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
    SDL_SetWindowFullscreen(window, 0);
  if((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0)
    SDL_RestoreWindow(window);

  SDL_SetWindowResizable(window, SDL_TRUE);
  SDL_SetWindowBordered(window, SDL_FALSE);
  SDL_SetWindowMinimumSize(window, 320, 200);
  SDL_SetWindowMaximumSize(window, 0, 0);

  SDL_SetWindowPosition(window, x, y);
  SDL_SetWindowSize(window, w, h);
  SDL_SetWindowPosition(window, x, y);
}

struct FreeStroke
{
  std::vector<ImVec2> points;
  float thickness = 2.2f;
  ImVec4 color = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
};

struct CopiedNoteItem
{
  CopiedNoteItem() = default;

  CopiedNoteItem(std::string item_title, std::string item_content)
      : title(std::move(item_title)),
        content(std::move(item_content))
  {
  }

  std::string title;
  std::string content;
  std::string font_path;
  float font_size = 0.0f;
  bool use_custom_color = false;
  float color_r = 0.0f, color_g = 0.0f, color_b = 0.0f;
  float width = 520.0f, height = 260.0f;
  bool has_layout = false;
  bool always_on_top = false;
};

struct CopiedFolderEntry
{
  std::string rel_path;
  std::vector<CopiedNoteItem> notes;
};

struct SidebarFlash
{
  ImVec4 color;
  double until = 0.0;
};

std::filesystem::path g_drawings_file;
std::filesystem::path g_clipboard_file;

std::unordered_map<std::string, std::vector<FreeStroke>> g_folder_drawings;
std::unordered_map<std::string, std::vector<std::vector<FreeStroke>>> g_draw_undo;
std::unordered_map<std::string, std::vector<std::vector<FreeStroke>>> g_draw_redo;
std::unordered_set<std::string> g_drawings_legacy_checked;
bool g_drawings_dirty = false;
bool g_has_copied_note = false;
std::string g_copied_note_title;
std::string g_copied_note_content;
std::vector<CopiedNoteItem> g_copied_notes_batch;
bool g_has_copied_folder = false;
std::string g_copied_folder_root_name;
std::vector<CopiedFolderEntry> g_copied_folder_entries;
bool g_clipboard_dirty = false;

atomic_file::SnapshotStore &g_project_files = atomic_file::shared_snapshot_store();
atomic_file::PersistenceGuard g_project_persistence_guard;
atomic_file::SaveBatch g_project_save_batch;
std::unordered_map<std::string, std::string> g_unresolved_persistence_errors;
std::string g_last_persistence_error;

bool record_project_save(const std::filesystem::path &path,
                         const atomic_file::SaveResult &result)
{
  g_project_save_batch.record(path, result);
  const std::string key = path.lexically_normal().generic_string();
  if(result)
  {
    g_unresolved_persistence_errors.erase(key);
    if(!result.message.empty())
    {
      LOG_WARNING("Saved '", path.generic_string(), "' with warning: ", result.message);
    }
    return true;
  }

  g_last_persistence_error = result.message;
  if(!result.recovery_path.empty())
    g_last_persistence_error += " Recovery: " + result.recovery_path.generic_string();
  g_unresolved_persistence_errors[key] = g_last_persistence_error;
  LOG_ERROR("Cannot save '", path.generic_string(), "': ", g_last_persistence_error);
  return false;
}

void track_project_file_move(const std::filesystem::path &from,
                             const std::filesystem::path &to)
{
  g_project_files.moved(from, to);
  atomic_file::move_path_value(g_unresolved_persistence_errors, from, to);
  g_project_persistence_guard.moved(from, to);
  MarkdownSupport::notify_document_moved(from, to);
}

bool record_project_save_error(const std::filesystem::path &path,
                               std::string message)
{
  atomic_file::SaveResult result;
  result.disposition = atomic_file::SaveDisposition::io_error;
  result.message = std::move(message);
  return record_project_save(path, result);
}

void record_project_read(const std::filesystem::path &path,
                         const atomic_file::ReadResult &result,
                         bool explicit_reload = false)
{
  if(explicit_reload)
    g_project_persistence_guard.record_reload(path, result);
  else
    g_project_persistence_guard.record_read(path, result);
  const std::string key = path.lexically_normal().generic_string();
  if(result && !g_project_persistence_guard.has_preserved_stale(path))
    g_unresolved_persistence_errors.erase(key);
  else if(!result)
    g_unresolved_persistence_errors[key] = result.message;
}

#ifdef NOTEPP_DEBUG_UI
static float g_dbg_swap_ms = 0.f;
static float g_dbg_begin_ms = 0.f;
static float g_dbg_ui_ms = 0.f;
static float g_dbg_end_ms = 0.f;
static unsigned int g_dbg_disk_reads = 0;
static unsigned int g_dbg_disk_reads_last_sec = 0;
static double g_dbg_next_metrics_time = 0.0;
#endif

struct ExplorerImageEntry
{
  std::string name;
  std::string path;
};

struct FolderImageCache
{
  std::vector<ExplorerImageEntry> images;
  bool valid = false;
};

static std::unordered_map<std::string, FolderImageCache> g_explorer_image_cache;

std::string read_text_file(const std::string &path)
{
#ifdef NOTEPP_DEBUG_UI
  ++g_dbg_disk_reads;
#endif
  const auto result = g_project_files.ensure_loaded(path);
  record_project_read(path, result);
  if(!result)
  {
    LOG_ERROR(result.message);
    return {};
  }
  return result.snapshot.content;
}

bool save_project_text(const std::filesystem::path &path, std::string_view content)
{
  if(path.empty() || atomic_file::shared_writes_suspended()) return false;
  if(g_project_persistence_guard.suppresses(path, content)) return false;

  if(!g_project_persistence_guard.may_write(path))
  {
    auto result = atomic_file::preserve_recovery(path, content);
    if(!g_project_persistence_guard.read_error(path).empty())
      result.message += "; canonical read failed: " +
                        g_project_persistence_guard.read_error(path);
    g_project_persistence_guard.record_save(path, content, result);
    return record_project_save(path, result);
  }

  const auto result = g_project_files.save(path, content);
  g_project_persistence_guard.record_save(path, content, result);
  return record_project_save(path, result);
}

bool write_text_file(const std::string &path, std::string_view content)
{
  const bool saved = save_project_text(path, content);
  if(saved) MarkdownSupport::notify_document_saved(path);
  return saved;
}

bool record_project_operation_error(const std::filesystem::path &path,
                                    std::string message)
{
  g_last_persistence_error = "Project file operation failed for '" +
                             path.generic_string() + "': " + std::move(message);
  LOG_ERROR(g_last_persistence_error);
  return false;
}

bool move_project_file(const std::filesystem::path &from,
                       const std::filesystem::path &to)
{
  if(atomic_file::shared_writes_suspended()) return false;
  if(from == to) return true;
  const auto loaded = g_project_files.ensure_loaded(from);
  record_project_read(from, loaded);
  if(!loaded)
    return record_project_operation_error(from, loaded.message);
  const auto moved = atomic_file::move_no_replace(from, to);
  if(!moved)
    return record_project_operation_error(to, moved.message);
  track_project_file_move(from, to);
  return true;
}

bool move_project_files(
    const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> &moves)
{
  for(const auto &[from, to] : moves)
  {
    std::error_code exists_error;
    if(std::filesystem::exists(to, exists_error) || exists_error)
    {
      record_project_operation_error(to, exists_error ? exists_error.message()
                                                      : "destination already exists");
      return false;
    }
    if(!std::filesystem::exists(from, exists_error) || exists_error)
    {
      record_project_operation_error(from, exists_error ? exists_error.message()
                                                        : "source does not exist");
      return false;
    }
  }

  std::size_t completed = 0;
  for(; completed < moves.size(); ++completed)
  {
    if(move_project_file(moves[completed].first, moves[completed].second)) continue;
    while(completed > 0)
    {
      --completed;
      if(!move_project_file(moves[completed].second, moves[completed].first))
      {
        LOG_ERROR("Cannot roll back partial project move");
      }
    }
    return false;
  }
  return true;
}

bool soft_delete_project_file(const std::filesystem::path &path)
{
  if(path.empty()) return true;
  std::error_code exists_error;
  if(!std::filesystem::exists(path, exists_error)) return !exists_error;
  const std::filesystem::path backup(path.string() + ".bak");
  return move_project_file(path, backup);
}

bool restore_project_backup(const std::filesystem::path &path)
{
  const std::filesystem::path backup(path.string() + ".bak");
  std::error_code exists_error;
  if(std::filesystem::exists(path, exists_error))
    return record_project_operation_error(path, "restore destination already exists");
  if(exists_error)
    return record_project_operation_error(path, exists_error.message());
  if(!std::filesystem::exists(backup, exists_error))
  {
    if(exists_error) return record_project_operation_error(backup, exists_error.message());
    return false;
  }
  return move_project_file(backup, path);
}

static bool is_image_file_ext(const std::filesystem::path &p)
{
  std::string ext = p.extension().string();
  for(auto &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".gif" || ext == ".bmp" || ext == ".webp";
}

static const std::vector<ExplorerImageEntry> &get_folder_images(
    const std::string &folder_name, const std::filesystem::path &folder_dir,
    bool force_refresh = false)
{
  auto &cache = g_explorer_image_cache[folder_name];
  if(!cache.valid || force_refresh)
  {
    cache.images.clear();
    std::error_code ec;
    if(std::filesystem::exists(folder_dir, ec))
    {
      for(const auto &entry : std::filesystem::directory_iterator(folder_dir, ec))
      {
        if(!entry.is_regular_file(ec)) continue;
        if(!is_image_file_ext(entry.path())) continue;
        cache.images.push_back({entry.path().filename().string(), entry.path().string()});
      }
      std::sort(cache.images.begin(), cache.images.end(),
                [](const ExplorerImageEntry &a, const ExplorerImageEntry &b) {
                  return a.name < b.name;
                });
    }
    cache.valid = true;
  }
  return cache.images;
}

static void invalidate_folder_image_cache(const std::string &folder_name)
{
  auto it = g_explorer_image_cache.find(folder_name);
  if(it != g_explorer_image_cache.end()) it->second.valid = false;
  MarkdownView::clear_sidebar_thumbnail_cache();
}

// --- Font file helpers ---

struct ExplorerFontEntry
{
  std::string name;
  std::string path;
};

struct FolderFontCache
{
  std::vector<ExplorerFontEntry> fonts;
  bool valid = false;
};

static std::unordered_map<std::string, FolderFontCache> g_explorer_font_cache;
static std::unordered_map<std::string, ImFont *> g_note_font_cache;
// Fonts added this frame but not yet in a rebuilt atlas; flushed in batch at frame start.
static std::unordered_map<std::string, ImFont *> g_note_font_pending;
static bool g_note_fonts_dirty = false;

static bool is_font_file_ext(const std::filesystem::path &p)
{
  auto ext = p.extension().string();
  for(auto &c : ext) c = (char)std::tolower((unsigned char)c);
  return ext == ".ttf" || ext == ".otf";
}

static const std::vector<ExplorerFontEntry> &get_folder_fonts(
    const std::string &folder_name, const std::filesystem::path &folder_dir)
{
  auto &cache = g_explorer_font_cache[folder_name];
  if(cache.valid) return cache.fonts;
  cache.fonts.clear();
  std::error_code ec;
  for(const auto &entry : std::filesystem::directory_iterator(folder_dir, ec))
  {
    if(!entry.is_regular_file(ec)) continue;
    if(!is_font_file_ext(entry.path())) continue;
    cache.fonts.push_back({entry.path().filename().string(), entry.path().string()});
  }
  std::sort(cache.fonts.begin(), cache.fonts.end(),
            [](const ExplorerFontEntry &a, const ExplorerFontEntry &b) {
              return a.name < b.name;
            });
  cache.valid = true;
  return cache.fonts;
}

static void invalidate_folder_font_cache(const std::string &folder_name)
{
  auto it = g_explorer_font_cache.find(folder_name);
  if(it != g_explorer_font_cache.end()) it->second.valid = false;
}

static ImFont *get_or_load_note_font(const std::string &abs_path, float size)
{
  char size_buf[16];
  std::snprintf(size_buf, sizeof(size_buf), "%.1f", size);
  const std::string cache_key = abs_path + ":" + size_buf;

  auto it = g_note_font_cache.find(cache_key);
  if(it != g_note_font_cache.end()) return it->second;

  // Already queued for the next batch rebuild — return nullptr until then.
  if(g_note_font_pending.count(cache_key)) return nullptr;

  ImGuiIO &io = ImGui::GetIO();
  ImFont *font = io.Fonts->AddFontFromFileTTF(abs_path.c_str(), size);
  if(font)
  {
    g_note_font_pending[cache_key] = font;
    g_note_fonts_dirty = true;
  }
  return nullptr;
}

// Rebuild the font atlas once after all new fonts have been queued this frame.
// Must be called between ImGui::Render() and the next ImGui_ImplOpenGL3_NewFrame().
static void flush_pending_note_fonts()
{
  if(!g_note_fonts_dirty) return;
#ifdef NOTEPP_DEBUG_UI
  const Uint64 t0 = SDL_GetPerformanceCounter();
#endif
  ImGuiIO &io = ImGui::GetIO();
  ImGui_ImplOpenGL3_DestroyFontsTexture();
  io.Fonts->Build();
  ImGui_ImplOpenGL3_CreateFontsTexture();
  for(auto &[key, font] : g_note_font_pending)
    g_note_font_cache[key] = font;
  g_note_font_pending.clear();
  g_note_fonts_dirty = false;
#ifdef NOTEPP_DEBUG_UI
  const float ms = (float)(SDL_GetPerformanceCounter() - t0) * 1000.f / (float)SDL_GetPerformanceFrequency();
  LOG_DEBUG("Font atlas rebuilt in ", ms, " ms");
#endif
}

static std::string read_binary_file(const std::filesystem::path &path)
{
  std::ifstream input(path, std::ios::binary);
  if(!input) return {};
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static std::string publish_binary_to_folder(const std::string &src_path,
                                            const std::filesystem::path &folder_dir,
                                            bool always_unique)
{
  const std::filesystem::path source(src_path);
  std::error_code error;
  if(!std::filesystem::is_regular_file(source, error) || error) return {};
  std::filesystem::create_directories(folder_dir, error);
  if(error) return {};

  const std::string content = read_binary_file(source);
  if(content.empty() && std::filesystem::file_size(source, error) != 0) return {};
  const std::string base_name = source.stem().string();
  const std::string extension = source.extension().string();
  std::filesystem::path destination = folder_dir / source.filename();
  int suffix = 2;
  while(std::filesystem::exists(destination, error))
  {
    if(error) return {};
    if(!always_unique && read_binary_file(destination) == content)
      return destination.string();
    destination = folder_dir / (base_name + "_" + std::to_string(suffix++) + extension);
  }
  if(error) return {};

  g_project_files.expect_missing(destination);
  if(!save_project_text(destination, content)) return {};
  return destination.string();
}

static std::string copy_font_to_folder(const std::string &src_path,
                                       const std::filesystem::path &folder_dir)
{
  return publish_binary_to_folder(src_path, folder_dir, false);
}

static void reveal_in_file_explorer(const std::string &file_path)
{
#if defined(_WIN32)
  std::wstring wpath = std::filesystem::path(file_path).wstring();
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wpath.c_str());
  if(pidl)
  {
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
  }
#elif defined(__APPLE__)
  pid_t pid = fork();
  if(pid == 0)
  {
    const char *args[] = {"open", "-R", file_path.c_str(), nullptr};
    execvp("open", (char *const *)args);
    _exit(127);
  }
  if(pid > 0) waitpid(pid, nullptr, 0);
#else
  const std::string dir = std::filesystem::path(file_path).parent_path().string();
  SDL_OpenURL(("file://" + dir).c_str());
#endif
}

static void open_directory(const std::filesystem::path &dir)
{
#if defined(_WIN32)
  std::wstring wpath = dir.wstring();
  ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::string dstr = dir.string();
  pid_t pid = fork();
  if(pid == 0)
  {
    const char *args[] = {"open", dstr.c_str(), nullptr};
    execvp("open", (char *const *)args);
    _exit(127);
  }
  if(pid > 0) waitpid(pid, nullptr, 0);
#else
  SDL_OpenURL(("file://" + dir.string()).c_str());
#endif
}

static std::string copy_image_to_folder(const std::string &src_path,
                                        const std::filesystem::path &folder_dir)
{
  return publish_binary_to_folder(src_path, folder_dir, true);
}

float dist2(ImVec2 a, ImVec2 b)
{
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return dx * dx + dy * dy;
}

SDL_Window *viewport_platform_window(const ImGuiViewport *viewport)
{
  if(viewport == nullptr || viewport->PlatformHandle == nullptr) return nullptr;
  const Uint32 window_id = (Uint32)(intptr_t)viewport->PlatformHandle;
  if(window_id == 0) return nullptr;
  return SDL_GetWindowFromID(window_id);
}

bool viewport_is_detached_from_main(const ImGuiViewport *viewport, SDL_Window *main_window)
{
  SDL_Window *platform_window = viewport_platform_window(viewport);
  return platform_window != nullptr && platform_window != main_window;
}

void apply_viewport_always_on_top(const ImGuiViewport *viewport, SDL_Window *main_window, bool always_on_top)
{
#if SDL_VERSION_ATLEAST(2, 0, 16)
  SDL_Window *platform_window = viewport_platform_window(viewport);
  if(platform_window != nullptr && platform_window != main_window)
  {
    SDL_SetWindowAlwaysOnTop(platform_window, always_on_top ? SDL_TRUE : SDL_FALSE);
  }
#else
  (void)viewport;
  (void)main_window;
  (void)always_on_top;
#endif
}

const ImGuiViewport *find_platform_viewport_by_id(const ImGuiPlatformIO &platform_io, ImGuiID viewport_id)
{
  for(ImGuiViewport *viewport : platform_io.Viewports)
  {
    if(viewport != nullptr && viewport->ID == viewport_id) return viewport;
  }
  return nullptr;
}

[[maybe_unused]] bool viewport_inherits_topmost(
    const ImGuiViewport *viewport,
    const ImGuiPlatformIO &platform_io,
    const std::unordered_set<unsigned int> &pinned_topmost_viewports)
{
  if(viewport == nullptr) return false;
  if((viewport->Flags & ImGuiViewportFlags_TopMost) != 0) return true;

  const ImGuiViewport *current = viewport;
  while(current != nullptr && current->ID != 0)
  {
    if(pinned_topmost_viewports.count(current->ID) != 0) return true;
    if(current->ParentViewportId == 0 || current->ParentViewportId == current->ID) break;
    current = find_platform_viewport_by_id(platform_io, current->ParentViewportId);
  }
  return false;
}

void set_detached_note_windows_enabled(bool enabled)
{
  ImGuiIO &io = ImGui::GetIO();
  if(enabled)
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  else
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
}

void set_dockers_enabled(bool enabled)
{
  (void)enabled;
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

ImGuiID saved_window_dock_id(const char *window_name)
{
  if(window_name == nullptr || window_name[0] == '\0') return 0;
  ImGuiWindowSettings *settings = ImGui::FindWindowSettingsByID(ImHashStr(window_name));
  return settings != nullptr ? settings->DockId : 0;
}

bool imgui_docking_enabled()
{
  return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0;
}

void load_drawings_state()
{
  g_folder_drawings.clear();
  g_draw_undo.clear();
  g_draw_redo.clear();
  g_drawings_legacy_checked.clear();

  const auto loaded = g_project_files.load(g_drawings_file);
  record_project_read(g_drawings_file, loaded, true);
  if(!loaded)
  {
    LOG_ERROR(loaded.message);
    return;
  }
  std::istringstream in(loaded.snapshot.content);
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
    float cr = 1.0f;
    float cg = 0.2f;
    float cb = 0.2f;
    float ca = 1.0f;
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
    s.color = ImVec4(clamp01f(cr), clamp01f(cg), clamp01f(cb), std::max(0.75f, clamp01f(ca)));
    s.points.reserve(static_cast<size_t>(count));

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

bool save_drawings_state()
{
  if(atomic_file::shared_writes_suspended()) return false;
  std::ostringstream out;
  for(const auto &[folder, strokes] : g_folder_drawings)
  {
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

  const bool saved = save_project_text(g_drawings_file, out.str());
  if(saved) g_drawings_dirty = false;
  return saved;
}

void load_note_clipboard(const AppConfig &config)
{
  g_has_copied_note = false;
  g_copied_note_title.clear();
  g_copied_note_content.clear();
  g_copied_notes_batch.clear();

  const auto loaded = atomic_file::read_text(g_clipboard_file);
  if(!loaded)
  {
    LOG_ERROR(loaded.message);
    return;
  }
  if(!loaded.snapshot.existed)
  {
    g_clipboard_dirty = false;
    return;
  }
  const Json document = Json::parse(loaded.snapshot.content, nullptr, false);
  const auto document_state = notepp::note_clipboard_state::validate_document(document, true);
  if(document_state != notepp::note_clipboard_state::DocumentState::supported)
  {
    LOG_ERROR(document_state == notepp::note_clipboard_state::DocumentState::future_schema
                  ? "note clipboard uses a newer unsupported schema"
                  : "note clipboard is malformed");
    return;
  }

  const int schema = notepp::note_index::read_schema_version(document);
  const std::vector<Json> items = notepp::note_clipboard_state::read_items(document);
  const notepp::project_paths::ProjectPaths paths(config.projectRoot);
  const std::string notes_top_level = project_notes_top_level(config);
  for(const Json &item : items)
  {
    if(!item.is_object()) continue;
    CopiedNoteItem copied;
    copied.title = item.value("title", std::string{});
    copied.content = item.value("content", std::string{});
    const std::string stored_font = item.value("font_path", std::string{});
    if(!stored_font.empty())
    {
      if(auto resolved = resolve_project_owned_path(paths, stored_font, schema, notes_top_level))
        copied.font_path = resolved->string();
      else if(schema < 2)
      {
        if(auto migrated = paths.migrate_legacy(stored_font, notes_top_level))
          copied.font_path = migrated->absolute_path.string();
      }
      if(copied.font_path.empty())
      {
        LOG_WARNING("Ignoring non-portable clipboard font path: ", stored_font);
        (void)0;
      }
    }
    copied.font_size = item.value("font_size", 0.0f);
    copied.use_custom_color = item.value("use_custom_color", false);
    copied.color_r = (float)item.value("color_r", 0) / 255.0f;
    copied.color_g = (float)item.value("color_g", 0) / 255.0f;
    copied.color_b = (float)item.value("color_b", 0) / 255.0f;
    copied.width = (float)item.value("w", 520);
    copied.height = (float)item.value("h", 260);
    copied.has_layout = item.value("has_layout", false);
    copied.always_on_top = item.value("always_on_top", false);
    if(!copied.content.empty()) g_copied_notes_batch.push_back(std::move(copied));
  }
  g_has_copied_note = !g_copied_notes_batch.empty();
  if(g_has_copied_note)
  {
    g_copied_note_title = g_copied_notes_batch.front().title;
    g_copied_note_content = g_copied_notes_batch.front().content;
  }
  g_clipboard_dirty = false;
}
} // namespace

void write_persistence_failure_report(const AppConfig &config)
{
  std::ostringstream report;
  report << "Notepp could not save every canonical project file.\n";
  for(const auto &[path, error] : g_unresolved_persistence_errors)
    report << path << ": " << error << '\n';
  const std::string preview_error = MarkdownSupport::last_persistence_error();
  if(!preview_error.empty()) report << preview_error << '\n';
  const std::string widget_error = MarkdownWidgets::last_persistence_error();
  if(!widget_error.empty()) report << widget_error << '\n';
  const auto saved = atomic_file::save_text(
      config.configPath / "last_persistence_error.txt", report.str());
  if(!saved)
  {
    LOG_ERROR("Cannot write persistence failure report: ", saved.message);
  }
}

bool App::save_workspace_text(const std::filesystem::path &path, std::string_view content)
{
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if(error)
  {
    LOG_ERROR("Cannot create local workspace directory '", path.parent_path().generic_string(),
              "': ", error.message());
    return false;
  }
  const auto saved = atomic_file::save_text(path, content);
  if(!saved)
  {
    LOG_ERROR("Cannot save local workspace file '", path.generic_string(), "': ", saved.message);
    return false;
  }
  return true;
}

void App::configure_workspace_paths()
{
  std::error_code error;
  std::filesystem::create_directories(config_.workspacePath, error);
  if(error)
    LOG_ERROR("Cannot create local workspace directory '",
              config_.workspacePath.generic_string(), "': ", error.message());

  default_state_file_ = config_.configPath / "note.md";
  legacy_state_meta_file_ = config_.configPath / "current_note_path.txt";
  index_file_ = config_.configPath / "notes_index.json";
  drawings_file_ = config_.configPath / "drawings_state.txt";
  imgui_ini_file_ = config_.workspacePath / "imgui_layout.ini";
  profiles_file_ = config_.workspacePath / "layout_profiles.json";
  workspace_file_ = config_.workspacePath / "workspace.json";
  g_clipboard_file = notepp::project::get_appdata_dir() / "note_clipboard.json";
  g_drawings_file = drawings_file_;
  state_file_path_ = default_state_file_.string();
  MarkdownSupport::set_preview_state_path(
      config_.workspacePath / "markdown_preview_state.json");
}

void App::migrate_legacy_workspace_files()
{
  legacy_workspace_migration_complete_ = true;
  const auto copy_if_missing = [](const std::filesystem::path &source,
                                  const std::filesystem::path &destination) {
    std::error_code destination_error;
    if(std::filesystem::exists(destination, destination_error)) return true;
    if(destination_error) return false;

    const auto legacy = atomic_file::read_text(source);
    if(!legacy) return false;
    if(!legacy.snapshot.existed) return true;

    std::error_code directory_error;
    std::filesystem::create_directories(destination.parent_path(), directory_error);
    if(directory_error) return false;

    const auto copied = atomic_file::save_text(destination, legacy.snapshot.content);
    return static_cast<bool>(copied);
  };
  const auto migrate = [this, &copy_if_missing](const std::filesystem::path &source,
                                                const std::filesystem::path &destination) {
    if(copy_if_missing(source, destination)) return;
    legacy_workspace_migration_complete_ = false;
    LOG_ERROR("Cannot migrate local workspace file '", source.generic_string(), "' to '",
              destination.generic_string(), "'");
  };

  migrate(config_.configPath / "layout_profiles.json", profiles_file_);
  migrate(config_.configPath / "markdown_preview_state.json",
          config_.workspacePath / "markdown_preview_state.json");
  migrate(config_.configPath / "imgui_layout.ini", imgui_ini_file_);
  migrate(config_.configPath / "note_clipboard.json", g_clipboard_file);
}
void App::load_workspace()
{
  workspace_writable_ = true;
  workspace_migration_complete_ = false;
  const auto loaded = atomic_file::read_text(workspace_file_);
  if(!loaded)
  {
    workspace_writable_ = false;
    LOG_ERROR("Cannot read local workspace file: ", loaded.message);
    return;
  }
  if(!loaded.snapshot.existed) return;

  const Json workspace = Json::parse(loaded.snapshot.content, nullptr, false);
  const auto invalid_workspace = [this] {
    workspace_writable_ = false;
    LOG_ERROR("Local workspace file is malformed and will not be overwritten: ",
              workspace_file_.generic_string());
  };
  if(!workspace.is_object())
  {
    invalid_workspace();
    return;
  }

  const auto has_invalid_type = [&workspace](const char *key,
                                             const auto predicate) {
    const auto found = workspace.find(key);
    return found != workspace.end() && !predicate(*found);
  };
  const auto schema_version = workspace.find("schemaVersion");
  if(schema_version == workspace.end() || !schema_version->is_number_integer() ||
     *schema_version != 1 ||
     has_invalid_type("activeFolder", [](const Json &value) { return value.is_string(); }) ||
     has_invalid_type("activeNoteId", [](const Json &value) { return value.is_string(); }) ||
     has_invalid_type("folderView", [](const Json &value) { return value.is_boolean(); }) ||
     has_invalid_type("layoutLocked", [](const Json &value) { return value.is_boolean(); }) ||
     has_invalid_type("detachedNoteWindows", [](const Json &value) {
       return value.is_boolean();
     }) ||
     has_invalid_type("dockersEnabled", [](const Json &value) { return value.is_boolean(); }) ||
     has_invalid_type("terminalHeight", [](const Json &value) { return value.is_number(); }) ||
     has_invalid_type("folders", [](const Json &value) { return value.is_object(); }))
  {
    invalid_workspace();
    return;
  }

  const auto folders_found = workspace.find("folders");
  if(folders_found != workspace.end())
  {
    for(auto it = folders_found->begin(); it != folders_found->end(); ++it)
    {
      if(!it.value().is_object())
      {
        invalid_workspace();
        return;
      }
      for(const char *key : {"layoutLocked", "detachedNoteWindows", "dockersEnabled",
                             "drawingsVisible", "gridVisible"})
      {
        const auto value = it.value().find(key);
        if(value != it.value().end() && !value->is_boolean())
        {
          invalid_workspace();
          return;
        }
      }
    }
  }

  const auto active_folder_found = workspace.find("activeFolder");
  if(active_folder_found != workspace.end())
  {
    const std::string &active_folder = active_folder_found->get_ref<const std::string &>();
    for(std::size_t index = 0; index < folders_.size(); ++index)
      if(folders_[index].name == active_folder)
      {
        active_folder_idx_ = static_cast<int>(index);
        break;
      }
  }
  const auto active_note_found = workspace.find("activeNoteId");
  if(active_note_found != workspace.end() && active_folder_idx_ >= 0 &&
     active_folder_idx_ < static_cast<int>(folders_.size()))
  {
    const std::string &active_note = active_note_found->get_ref<const std::string &>();
    for(std::size_t index = 0;
        index < folders_[static_cast<std::size_t>(active_folder_idx_)].notes.size(); ++index)
      if(folders_[static_cast<std::size_t>(active_folder_idx_)].notes[index].id == active_note)
      {
        active_note_idx_ = static_cast<int>(index);
        break;
      }
  }
  if(const auto found = workspace.find("folderView"); found != workspace.end())
    folder_overview_mode_ = found->get<bool>();
  if(const auto found = workspace.find("layoutLocked"); found != workspace.end())
    layout_locked_ = found->get<bool>();
  if(const auto found = workspace.find("detachedNoteWindows"); found != workspace.end())
    detached_note_windows_enabled_ = found->get<bool>();
  if(const auto found = workspace.find("dockersEnabled"); found != workspace.end())
    dockers_enabled_ = found->get<bool>();
  if(const auto found = workspace.find("terminalHeight"); found != workspace.end())
    terminal_height_ = std::clamp(found->get<float>(), 180.0F, 2000.0F);

  if(folders_found == workspace.end()) return;
  for(FolderMeta &folder : folders_)
  {
    const auto found = folders_found->find(folder.name);
    if(found == folders_found->end()) continue;
    const Json &settings = *found;
    if(const auto value = settings.find("layoutLocked"); value != settings.end())
      folder.layout_locked = value->get<bool>();
    if(const auto value = settings.find("detachedNoteWindows"); value != settings.end())
      folder.detached_note_windows = value->get<bool>();
    if(const auto value = settings.find("dockersEnabled"); value != settings.end())
      folder.dockers_enabled = value->get<bool>();
    if(const auto value = settings.find("drawingsVisible"); value != settings.end())
      folder.drawings_visible = value->get<bool>();
    if(const auto value = settings.find("gridVisible"); value != settings.end())
      folder.grid_visible = value->get<bool>();
  }
}
bool App::save_workspace()
{
  if(!workspace_writable_)
  {
    LOG_ERROR("Local workspace is read-only until its load error is resolved");
    return false;
  }
  sync_active_folder_settings();
  Json workspace = Json::object();
  workspace["schemaVersion"] = 1;
  if(active_folder_idx_ >= 0 && active_folder_idx_ < static_cast<int>(folders_.size()))
  {
    const FolderMeta &folder = folders_[static_cast<std::size_t>(active_folder_idx_)];
    workspace["activeFolder"] = folder.name;
    if(active_note_idx_ >= 0 && active_note_idx_ < static_cast<int>(folder.notes.size()))
      workspace["activeNoteId"] = folder.notes[static_cast<std::size_t>(active_note_idx_)].id;
  }
  workspace["folderView"] = folder_overview_mode_;
  workspace["layoutLocked"] = layout_locked_;
  workspace["detachedNoteWindows"] = detached_note_windows_enabled_;
  workspace["dockersEnabled"] = dockers_enabled_;
  workspace["terminalHeight"] = terminal_height_;
  Json folders = Json::object();
  for(const FolderMeta &folder : folders_)
    folders[folder.name] = {{"layoutLocked", folder.layout_locked},
                            {"detachedNoteWindows", folder.detached_note_windows},
                            {"dockersEnabled", folder.dockers_enabled},
                            {"drawingsVisible", folder.drawings_visible},
                            {"gridVisible", folder.grid_visible}};
  workspace["folders"] = std::move(folders);
  std::string serialized = workspace.dump(2);
  serialized.push_back('\n');
  return save_workspace_text(workspace_file_, serialized);
}

bool App::save_note_clipboard()
{
  if(atomic_file::shared_writes_suspended()) return false;
  std::vector<Json> persisted_items;
  persisted_items.reserve(g_copied_notes_batch.size());
  const notepp::project_paths::ProjectPaths paths(config_.projectRoot);
  const std::string notes_top_level = project_notes_top_level(config_);
  for(const CopiedNoteItem &copied : g_copied_notes_batch)
  {
    Json item = {{"title", copied.title},
                 {"content", copied.content},
                 {"use_custom_color", copied.use_custom_color},
                 {"color_r", (int)std::lround(std::clamp(copied.color_r, 0.0f, 1.0f) * 255.0f)},
                 {"color_g", (int)std::lround(std::clamp(copied.color_g, 0.0f, 1.0f) * 255.0f)},
                 {"color_b", (int)std::lround(std::clamp(copied.color_b, 0.0f, 1.0f) * 255.0f)},
                 {"w", (int)std::lround(copied.width)},
                 {"h", (int)std::lround(copied.height)},
                 {"has_layout", copied.has_layout},
                 {"always_on_top", copied.always_on_top}};
    if(copied.font_size > 0.0f) item["font_size"] = copied.font_size;
    if(!copied.font_path.empty())
    {
      const auto encoded = portable_project_path(paths, copied.font_path, notes_top_level);
      if(encoded)
        item["font_path"] = *encoded;
      else
      {
        LOG_WARNING("Omitting non-portable clipboard font path: ", copied.font_path);
        (void)0;
      }
    }
    persisted_items.push_back(std::move(item));
  }
  const Json root = notepp::note_clipboard_state::make_document(persisted_items);
  std::string serialized = root.dump(2);
  serialized.push_back('\n');
  const bool saved = save_workspace_text(g_clipboard_file, serialized);
  if(saved) g_clipboard_dirty = false;
  return saved;
}

App::App(AppConfig config)
    : config_(std::move(config)),
      app_settings_store_(config_.appSettingsPath),
      project_settings_store_(config_.configPath / "project_settings.json"),
      git_client_(git_process_runner_)
{
  const auto app_settings = app_settings_store_.load();
  if(!app_settings)
  {
    app_settings_error_ = app_settings.message;
    LOG_ERROR("Cannot load Notepp app settings: ", app_settings_error_);
  }
  if(config_.hasInitialGitStatus)
  {
    git_status_ = config_.initialGitStatus;
    git_status_available_ = true;
  }
  else if(app_settings && !app_settings.settings.last_git_sync.state.empty())
  {
    git_status_.state = notepp::git_sync::state_from_name(app_settings.settings.last_git_sync.state);
    git_status_.summary = app_settings.settings.last_git_sync.summary;
    git_status_.detail = app_settings.settings.last_git_sync.detail;
    git_status_.checked_at = std::chrono::system_clock::now();
    git_last_attempt_ = app_settings.settings.last_git_sync.attempted_at;
    git_status_available_ = true;
  }

  g_project_files.clear();
  g_project_persistence_guard.clear();
  g_project_save_batch.clear();
  g_unresolved_persistence_errors.clear();
  g_last_persistence_error.clear();
  MarkdownWidgets::reset_persistence_state();
  std::filesystem::create_directories(config_.dataPath);
  std::filesystem::create_directories(config_.configPath);
  configure_workspace_paths();

  rebind_command_ipc();
  migrate_legacy_workspace_files();
  MarkdownView::set_document_path(config_.dataPath);
  MarkdownView::set_assets_path(config_.assetsPath);
  MarkdownWidgets::set_terminal_command_handler([this](std::string_view command) {
    terminal_visible_ = true;
    request_open_terminal_ = false;
    terminal_.setDefaultWorkingDirectory(config_.dataPath);
    if(terminal_.sessionCount() == 0) terminal_.start(config_.dataPath, 24, 80);

    std::string input(command);
    if(input.empty() || input.back() != '\n') input.push_back('\n');
    terminal_.write(input);
    dirty_ = true;
  });
  MarkdownWidgets::set_command_action_handler([this](std::string_view command,
                                                     std::string_view) {
    std::vector<std::string> words;
    if(!split_command_words(command, words))
    {
      LOG_WARNING("Kanban command has an unterminated quote");
      return true;
    }
    if(words.size() < 3U || words[0] != "note" || words[1] != "color" ||
       words[2] != "set")
      return false;

    // Friendly note commands are handled in-process. Legacy notepp-cli and
    // other shell commands intentionally return false and use the terminal.
    nlohmann::json args = nlohmann::json::object();
    std::unordered_set<std::string> seen_options;
    for(std::size_t i = 3; i < words.size();)
    {
      const std::string &option = words[i];
      const std::string canonical_option = option == "--path" ? "--note" : option;
      if(canonical_option != "--note" && canonical_option != "--color")
      {
        LOG_WARNING("Kanban note color command has an unknown option: ", option);
        return true;
      }
      if(!seen_options.insert(canonical_option).second)
      {
        LOG_WARNING("Kanban note color command has a duplicate option: ", option);
        return true;
      }
      if(i + 1U >= words.size() || words[i + 1U].rfind("--", 0) == 0)
      {
        LOG_WARNING("Kanban note color command is missing a value for: ", option);
        return true;
      }
      const std::string &value = words[i + 1U];
      if(canonical_option == "--note")
      {
        const std::size_t fragment = value.find('#');
        const std::string reference = value.substr(0, fragment);
        if(reference.empty())
        {
          LOG_WARNING("Kanban note color command has an empty note reference");
          return true;
        }

        bool found_id = false;
        const std::string href_path = std::filesystem::path(reference).lexically_normal().generic_string();
        for(const auto &folder : folders_)
          for(const auto &note : folder.notes)
          {
            if(note.id == reference)
            {
              args["id"] = note.id;
              found_id = true;
              break;
            }
            const std::filesystem::path stored_path(note.path);
            std::string loaded_path;
            if(stored_path.is_absolute())
            {
              std::error_code relative_error;
              const std::filesystem::path relative_path =
                  std::filesystem::relative(stored_path, config_.dataPath, relative_error);
              if(relative_error) continue;
              loaded_path = relative_path.lexically_normal().generic_string();
            }
            else
              loaded_path = stored_path.lexically_normal().generic_string();
            if(loaded_path == href_path)
            {
              args["id"] = note.id;
              found_id = true;
              break;
            }
          }
        if(!found_id)
        {
          if(reference.find('/') != std::string::npos || reference.find('\\') != std::string::npos ||
             (reference.size() > 3U && reference.substr(reference.size() - 3U) == ".md"))
            args["path"] = reference;
          else
            args["name"] = reference;
        }
      }
      else
      {
        int r = 0, g = 0, b = 0;
        if(!parse_hex_color(value, r, g, b))
        {
          LOG_WARNING("Kanban note color command has an invalid color: ", value);
          return true;
        }
        args["r"] = r;
        args["g"] = g;
        args["b"] = b;
      }
      i += 2U;
    }
    if(!args.contains("id") && !args.contains("path") && !args.contains("name"))
    {
      LOG_WARNING("Kanban note color command is missing --note");
      return true;
    }
    if(!args.contains("r"))
    {
      LOG_WARNING("Kanban note color command is missing --color");
      return true;
    }
    if(command_api_ == nullptr)
    {
      LOG_WARNING("Command API is unavailable for kanban action");
      return true;
    }
    const nlohmann::json request{{"command", "note.color.set"}, {"args", args}};
    const auto response = command_api_->execute(request);
    if(response.ok)
      post_command_mutation(request, response);
    else
      LOG_WARNING("Kanban note color command failed: ", response.body.dump());
    return true;
  });

  Lang::init(config_.assetsPath / "languages");
  if(!load_project_settings())
    LOG_ERROR("Cannot load project settings: ", project_settings_error_);
}

void App::rebind_command_ipc()
{
  command_ipc_server_.close();
  notepp::project::ProjectInfo command_project;
  command_project.root = config_.projectRoot;
  command_project.notes = config_.dataPath;
  command_project.assets = config_.assetsPath;
  command_project.config = config_.configPath;
  command_project.workspace = config_.workspacePath;
  command_project.projectFile = config_.projectRoot / "notepp.project.json";
  const std::string command_endpoint = notepp::command_api::endpoint_for_project(command_project);
  command_api_ = std::make_unique<notepp::command_api::Api>(std::move(command_project));
  command_api_->set_variable_adapter({
      [](const std::filesystem::path &path, std::string_view markdown, std::string_view name) {
        MarkdownWidgets::set_widget_document_path(path);
        const auto result = MarkdownWidgets::command_get_variable(markdown, name);
        return notepp::command_api::VariableResult{result.success, result.value, result.error};
      },
      [](const std::filesystem::path &path, std::string &markdown, std::string_view name,
         const nlohmann::json &value) {
        MarkdownWidgets::set_widget_document_path(path);
        const auto result = MarkdownWidgets::command_set_variable(markdown, name, value);
        return notepp::command_api::VariableResult{result.success, result.value, result.error};
      }});
  std::string command_ipc_error;
  if(!command_ipc_server_.open(command_endpoint, &command_ipc_error))
    LOG_WARNING("Command IPC unavailable: ", command_ipc_error);
}

void App::post_command_mutation(const nlohmann::json &request,
                                const notepp::command_api::Response &response)
{
  if(frame_ui_active_)
  {
    // Note rendering keeps references into folders_. Reloading here (for
    // example from a Mermaid kanban callback) would invalidate those
    // references before the rest of the frame has finished drawing.
    pending_command_mutations_.push_back(PendingCommandMutation{request, response});
    return;
  }

  (void)response;
  const std::string command = request.value("command", request.value("method", std::string{}));
  const bool mutates = command == "note.create" || command == "note.header.create" ||
                       command == "note.color.set" || command == "note.variable.set";
  if(!mutates) return;

  // The command API writes through atomic_file while the editor may hold an
  // older in-memory document. Reloading at this frame boundary prevents a
  // later GUI save from overwriting the command result. Unsaved editor text
  // is deliberately discarded for this initial synchronization path.
  note_content_cache_.clear();
  g_project_files.clear();
  MarkdownView::clear_sidebar_thumbnail_cache();
  MarkdownWidgets::reset_persistence_state();
  discard_pending_text_history();
  load_state();
  dirty_ = true;
}

void App::flush_pending_command_mutations()
{
  std::vector<PendingCommandMutation> pending = std::move(pending_command_mutations_);
  pending_command_mutations_.clear();
  for(const PendingCommandMutation &mutation : pending)
    post_command_mutation(mutation.request, mutation.response);
}

bool App::load_project_settings()
{
  const auto loaded = project_settings_store_.load(false);
  if(!loaded)
  {
    project_settings_error_ = loaded.message;
    return false;
  }

  const auto app_settings = app_settings_store_.load();
  const bool migrate_git_sync = !loaded.settings.has_git_sync_enabled && app_settings &&
                                app_settings.settings.git_sync_enabled;

  project_settings_error_.clear();
  git_sync_enabled_ = loaded.settings.git_sync_enabled || migrate_git_sync;
  project_language_explicit_ = false;
  if(!loaded.settings.language.empty())
  {
    project_language_explicit_ = Lang::set_language(loaded.settings.language);
    if(!project_language_explicit_)
      LOG_WARNING("Project language is unavailable: ", loaded.settings.language);
  }
  project_settings_dirty_ = migrate_git_sync;
  return true;
}

void App::record_git_status(const notepp::git_sync::Status &status)
{
  git_status_ = status;
  git_status_available_ = true;
  git_last_attempt_ = notepp::app_settings::current_utc_timestamp();
  const notepp::app_settings::GitSyncRecord record{
      std::string(notepp::git_sync::state_name(status.state)), status.summary, status.detail,
      git_last_attempt_};
  const auto saved = app_settings_store_.record_git_sync_status(record);
  if(!saved)
  {
    app_settings_error_ = saved.message;
    LOG_ERROR("Cannot persist Git Sync status: ", saved.message);
  }
  if(status.state == notepp::git_sync::SyncState::clean)
  {
    LOG_INFO("Git Sync: ", status.summary);
  }
  else
  {
    LOG_ERROR("Git Sync: ", status.summary, status.detail.empty() ? "" : " - ", status.detail);
  }
  dirty_ = true;
}

bool App::save_imgui_settings()
{
  if(atomic_file::shared_writes_suspended()) return false;
  if(!ImGui::GetCurrentContext()) return true;
  std::size_t ini_size = 0;
  const char *ini_data = ImGui::SaveIniSettingsToMemory(&ini_size);
  return save_workspace_text(
      imgui_ini_file_, std::string_view(ini_data != nullptr ? ini_data : "", ini_size));
}

bool App::save_project_settings()
{
  if(!project_settings_dirty_) return true;
  if(atomic_file::shared_writes_suspended()) return false;

  const auto saved_language = project_settings_store_.set_language(Lang::current_language_code());
  if(!saved_language)
  {
    project_settings_error_ = saved_language.message;
    LOG_ERROR("Cannot save project language: ", project_settings_error_);
    return false;
  }
  const auto saved_git_sync = project_settings_store_.set_git_sync_enabled(git_sync_enabled_);
  if(!saved_git_sync)
  {
    project_settings_error_ = saved_git_sync.message;
    LOG_ERROR("Cannot save Git Sync setting: ", project_settings_error_);
    return false;
  }
  project_settings_error_.clear();
  project_settings_dirty_ = false;
  return true;
}

void App::begin_git_operation(bool manual_sync)
{
  if(git_sync_in_progress_ || close_requested_) return;
  if(!git_sync_enabled_)
  {
    notepp::git_sync::Status disabled;
    disabled.state = notepp::git_sync::SyncState::error;
    disabled.summary = Lang::t("Git Sync is disabled");
    record_git_status(disabled);
    return;
  }
  if(manual_sync && !save_imgui_settings())
    LOG_ERROR("Cannot save local ImGui workspace state before Git Sync");
  if(manual_sync && !save_state())
  {
    notepp::git_sync::Status failed;
    failed.state = notepp::git_sync::SyncState::conflict;
    failed.summary = Lang::t("Git Sync skipped because project save failed");
    failed.detail = g_last_persistence_error;
    record_git_status(failed);
    return;
  }

  git_sync_in_progress_ = true;
  git_operation_kind_ = manual_sync ? GitOperationKind::manual : GitOperationKind::inspect;
  atomic_file::set_shared_writes_suspended(true);
  git_status_available_ = true;
  git_status_.state = notepp::git_sync::SyncState::syncing;
  git_status_.summary = Lang::t("Git Sync in progress");
  const std::filesystem::path root = config_.projectRoot;
  try
  {
    git_sync_future_ = std::async(std::launch::async, [this, root, manual_sync] {
      notepp::git_sync::OperationResult result;
      try
      {
        if(manual_sync)
          result = git_client_.manual_sync(
              root, "Notepp sync " + notepp::app_settings::current_utc_timestamp());
        else
        {
          result.status = git_client_.inspect(root);
          result.success = result.status.state == notepp::git_sync::SyncState::clean ||
                           result.status.state == notepp::git_sync::SyncState::dirty ||
                           result.status.state == notepp::git_sync::SyncState::ahead ||
                           result.status.state == notepp::git_sync::SyncState::behind;
        }
      }
      catch(const std::exception &error)
      {
        result = notepp::git_sync::exception_result("Git Sync", error.what());
      }
      catch(...)
      {
        result = notepp::git_sync::exception_result("Git Sync");
      }
      SDL_Event event{};
      event.type = kGitSyncCompletedEvent;
      SDL_PushEvent(&event);
      return GitAsyncResult{std::move(result), {}};
    });
  }
  catch(const std::exception &error)
  {
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
    record_git_status(notepp::git_sync::exception_result(
                          "Starting Git Sync", error.what())
                          .status);
  }
  catch(...)
  {
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
    record_git_status(notepp::git_sync::exception_result("Starting Git Sync").status);
  }
}

void App::begin_close_git_operation()
{
  git_sync_in_progress_ = true;
  git_operation_kind_ = GitOperationKind::close;
  atomic_file::set_shared_writes_suspended(true);
  git_status_available_ = true;
  git_status_.state = notepp::git_sync::SyncState::syncing;
  git_status_.summary = Lang::t("Git Sync in progress");
  const std::filesystem::path root = config_.projectRoot;
  try
  {
    git_sync_future_ = std::async(std::launch::async, [this, root] {
      notepp::git_sync::OperationResult result;
      try
      {
        result = git_client_.commit_and_push(
            root, "Notepp sync " + notepp::app_settings::current_utc_timestamp());
      }
      catch(const std::exception &error)
      {
        result = notepp::git_sync::exception_result("Closing Git Sync", error.what());
      }
      catch(...)
      {
        result = notepp::git_sync::exception_result("Closing Git Sync");
      }
      SDL_Event event{};
      event.type = kGitSyncCompletedEvent;
      SDL_PushEvent(&event);
      return GitAsyncResult{std::move(result), {}};
    });
  }
  catch(const std::exception &error)
  {
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
    record_git_status(
        notepp::git_sync::exception_result("Starting close Git Sync", error.what()).status);
    running_ = false;
  }
  catch(...)
  {
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
    record_git_status(notepp::git_sync::exception_result("Starting close Git Sync").status);
    running_ = false;
  }
}

void App::request_close()
{
  close_requested_ = true;
  dirty_ = true;
}

void App::advance_close()
{
  using notepp::sync_coordinator::CloseAction;
  CloseAction action = notepp::sync_coordinator::next_close_action(
      close_requested_, git_sync_in_progress_, close_persisted_,
      git_sync_enabled_, close_save_succeeded_);
  if(action == CloseAction::persist)
  {
    close_save_succeeded_ = persist_before_close();
    close_persisted_ = true;
    action = notepp::sync_coordinator::next_close_action(
        close_requested_, git_sync_in_progress_, close_persisted_,
        git_sync_enabled_, close_save_succeeded_);
  }
  if(action == CloseAction::start_git)
  {
    begin_close_git_operation();
  }
  else if(action == CloseAction::exit)
  {
    atomic_file::set_shared_writes_suspended(true);
    running_ = false;
  }
}

void App::reload_project_after_git_pull()
{
  // Git changed a coherent project tree. Any edits made while the operation was
  // running are first preserved through checked saves; stale files become visible
  // recovery copies rather than overwriting pulled data.
  if(state_dirty_ || layout_dirty_ || g_drawings_dirty || g_clipboard_dirty)
    (void)save_state();
  MarkdownWidgets::reset_persistence_state();
  MarkdownSupport::set_preview_state_path(
      config_.workspacePath / "markdown_preview_state.json");
  load_state();
  const auto ini_snapshot = atomic_file::read_text(imgui_ini_file_);
  if(ini_snapshot && ini_snapshot.snapshot.existed)
    ImGui::LoadIniSettingsFromMemory(ini_snapshot.snapshot.content.data(),
                                     ini_snapshot.snapshot.content.size());
  g_explorer_image_cache.clear();
  g_explorer_font_cache.clear();
  MarkdownView::clear_sidebar_thumbnail_cache();
  reset_sidebar_state_ = true;
}

void App::finish_git_operation()
{
  if(!git_sync_in_progress_ || !git_sync_future_.valid()) return;
  const GitOperationKind completed_kind = git_operation_kind_;
  GitAsyncResult async_result;
  try
  {
    async_result = git_sync_future_.get();
  }
  catch(const std::exception &error)
  {
    async_result.operation = notepp::git_sync::exception_result("Completing Git Sync", error.what());
  }
  catch(...)
  {
    async_result.operation = notepp::git_sync::exception_result("Completing Git Sync");
  }
  git_sync_in_progress_ = false;
  git_operation_kind_ = GitOperationKind::none;
  atomic_file::set_shared_writes_suspended(false);
#if USE_PORTABLE_PATHS
  if(!async_result.switch_root.empty())
  {
    load_switched_project(async_result.switch_root);
    // The project store is replaced by load_switched_project, so persist the
    // switch result in the newly selected project's settings.
    record_git_status(async_result.operation.status);
    return;
  }
#endif
  record_git_status(async_result.operation.status);
  if(async_result.operation.success && async_result.operation.changed_worktree)
    reload_project_after_git_pull();

  if(completed_kind == GitOperationKind::close)
  {
    // No project writer may run after the authoritative close-push result.
    atomic_file::set_shared_writes_suspended(true);
    running_ = false;
    return;
  }
  if(close_requested_)
  {
    advance_close();
    return;
  }
  if(async_result.operation.success && async_result.operation.changed_worktree)
  {
    // Reload/migration may legitimately write portable metadata. Inspect again
    // asynchronously so the badge cannot remain falsely "Synced".
    begin_git_operation(false);
  }
}

#if USE_PORTABLE_PATHS
void App::load_switched_project(const std::filesystem::path &new_root)
{
  g_project_files.clear();
  g_project_persistence_guard.clear();
  g_project_save_batch.clear();
  g_unresolved_persistence_errors.clear();
  g_last_persistence_error.clear();
  MarkdownWidgets::reset_persistence_state();

  const auto project = notepp::project::create_or_open_project(new_root);
  config_.projectRoot = project.root;
  config_.dataPath = project.notes;
  config_.configPath = project.config;
  config_.workspacePath = project.workspace;
  configure_workspace_paths();
  rebind_command_ipc();
  migrate_legacy_workspace_files();
  project_settings_store_ = notepp::project_settings::Store(
      config_.configPath / "project_settings.json");
  project_language_explicit_ = false;
  git_sync_enabled_ = false;
  git_last_attempt_.clear();
  git_status_available_ = false;
  config_.hasInitialGitStatus = false;
  if(!load_project_settings())
    LOG_ERROR("Cannot load project settings: ", project_settings_error_);

  g_folder_drawings.clear();
  g_draw_undo.clear();
  g_draw_redo.clear();
  g_drawings_legacy_checked.clear();
  g_drawings_dirty = false;
  g_explorer_image_cache.clear();
  g_explorer_font_cache.clear();
  MarkdownView::clear_sidebar_thumbnail_cache();
  MarkdownView::set_document_path(config_.dataPath);
  terminal_.setDefaultWorkingDirectory(config_.dataPath);
  note_title_ = "Note";
  markdown_text_.clear();
  reset_sidebar_state_ = true;
  load_state();

  const auto ini_snapshot = atomic_file::read_text(imgui_ini_file_);
  if(ini_snapshot && ini_snapshot.snapshot.existed)
    ImGui::LoadIniSettingsFromMemory(ini_snapshot.snapshot.content.data(),
                                     ini_snapshot.snapshot.content.size());
}

bool App::switch_project(const std::filesystem::path &new_root)
{
  if(git_sync_in_progress_) return false;
  if(!save_project_settings())
  {
    LOG_ERROR("Project switch cancelled because project settings could not be saved");
    return false;
  }
  if(!save_state())
  {
    LOG_ERROR("Project switch cancelled because project files could not be saved");
    return false;
  }
  if(!save_imgui_settings())
    LOG_ERROR("Cannot save local ImGui workspace state before project switch");
  notepp::project_settings::Store new_project_settings(
      new_root / "config" / "project_settings.json");
  const auto new_settings = new_project_settings.load(false);
  const bool new_project_may_need_pull =
      !new_settings || !new_settings.existed || !new_settings.settings.has_git_sync_enabled ||
      new_settings.settings.git_sync_enabled;
  if(!git_sync_enabled_ && !new_project_may_need_pull)
  {
    load_switched_project(new_root);
    return true;
  }

  git_sync_in_progress_ = true;
  git_operation_kind_ = GitOperationKind::project_switch;
  atomic_file::set_shared_writes_suspended(true);
  git_status_available_ = true;
  git_status_.state = notepp::git_sync::SyncState::syncing;
  git_status_.summary = Lang::t("Git Sync in progress");
  const std::filesystem::path old_root = config_.projectRoot;
  const bool old_project_git_sync_enabled = git_sync_enabled_;
  try
  {
    git_sync_future_ = std::async(std::launch::async,
                                  [this, old_root, new_root, old_project_git_sync_enabled] {
      notepp::git_sync::OperationResult result;
      try
      {
        notepp::git_sync::OperationResult pushed;
        pushed.success = true;
        pushed.status.state = notepp::git_sync::SyncState::unavailable;
        pushed.status.summary = "Project Git Sync is disabled";
        if(old_project_git_sync_enabled)
          pushed = git_client_.commit_and_push(
              old_root, "Notepp sync " + notepp::app_settings::current_utc_timestamp());
        notepp::project_settings::Store new_project_settings(
            new_root / "config" / "project_settings.json");
        const auto new_settings = new_project_settings.load(false);
        const bool pull_enabled =
            !new_settings || !new_settings.existed || !new_settings.settings.has_git_sync_enabled ||
            new_settings.settings.git_sync_enabled;
        notepp::git_sync::OperationResult pulled;
        if(pull_enabled)
          pulled = git_client_.pull_on_open(new_root);
        else
        {
          pulled.success = true;
          pulled.status.state = notepp::git_sync::SyncState::unavailable;
          pulled.status.summary = "Project Git Sync is disabled";
        }
        result = pulled;
        if(!pushed.success && !pull_enabled)
          result = pushed;
        else if(!pushed.success && pulled.success)
          result = pushed;
        else if(!pushed.success && !pulled.success)
        {
          result = notepp::git_sync::exception_result(
              "Project switch Git Sync",
              "Old project push: " + pushed.status.summary + " - " + pushed.status.detail +
                  "\nNew project pull: " + pulled.status.summary + " - " + pulled.status.detail);
        }
      }
      catch(const std::exception &error)
      {
        result = notepp::git_sync::exception_result("Project switch Git Sync", error.what());
      }
      catch(...)
      {
        result = notepp::git_sync::exception_result("Project switch Git Sync");
      }
      SDL_Event event{};
      event.type = kGitSyncCompletedEvent;
      SDL_PushEvent(&event);
      return GitAsyncResult{std::move(result), new_root};
    });
  }
  catch(const std::exception &error)
  {
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
    const auto status = notepp::git_sync::exception_result(
                            "Starting project switch Git Sync", error.what())
                            .status;
    load_switched_project(new_root);
    record_git_status(status);
  }
  catch(...)
  {
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
    const auto status = notepp::git_sync::exception_result(
                            "Starting project switch Git Sync")
                            .status;
    load_switched_project(new_root);
    record_git_status(status);
  }
  return true;
}
#endif

bool App::persist_before_close()
{
  bool saved = save_project_settings();
  layout_profiles_.erase(
      std::remove_if(layout_profiles_.begin(), layout_profiles_.end(),
                     [](const LayoutProfile &profile) { return profile.pending_delete; }),
      layout_profiles_.end());
  if(!layout_profiles_.empty()) capture_to_active_profile();

  const bool state_saved = save_state();
  saved = saved && state_saved;
  if(!save_imgui_settings()) LOG_ERROR("Cannot save local ImGui workspace state");
  saved = saved && g_unresolved_persistence_errors.empty() &&
          MarkdownWidgets::last_persistence_error().empty();
  last_save_succeeded_ = saved;

  if(saved)
  {
    std::unordered_set<std::string> alive_paths;
    for(const FolderMeta &folder : folders_)
      for(const NoteMeta &note : folder.notes)
        if(!note.path.empty()) alive_paths.insert(note.path);
    for(const std::string &path : pending_fs_delete_paths_)
    {
      if(path.empty() || alive_paths.contains(path)) continue;
      std::error_code remove_error;
      std::filesystem::remove(std::filesystem::path(path + ".bak"), remove_error);
      if(remove_error)
      {
        LOG_WARNING("Cannot remove backup '", path, ".bak': ", remove_error.message());
      }
    }
    pending_fs_delete_paths_.clear();
  }
  else
  {
    LOG_ERROR("Project close retained recovery files because persistence was incomplete");
    write_persistence_failure_report(config_);
  }
  return saved;
}

int App::run()
{
  try
  {
    init_sdl_gl();
    init_imgui();
    load_state();
    const auto ini_snapshot = atomic_file::read_text(imgui_ini_file_);
    if(!ini_snapshot)
    {
      LOG_ERROR(ini_snapshot.message);
    }
    else if(ini_snapshot.snapshot.existed)
      ImGui::LoadIniSettingsFromMemory(ini_snapshot.snapshot.content.data(),
                                       ini_snapshot.snapshot.content.size());

    // Low-frequency filesystem invalidation: wakes the event loop to scan for
    // external note/image changes, but only renders when the scan finds changes.
    file_watch_timer_ = SDL_AddTimer(1000, project_file_watch_timer_callback, nullptr);

    // Extra frames to render after the last event or interaction, so hover effects
    // and frame-delayed state (popup close, tooltip fade) settle cleanly.
    int keep_alive_frames = 2;

    while(running_)
    {
#ifdef NOTEPP_DEBUG_UI
      const Uint64 dbg_t0 = SDL_GetPerformanceCounter();
#endif
      const bool had_event = frame_begin();
#ifdef NOTEPP_DEBUG_UI
      const Uint64 dbg_t1 = SDL_GetPerformanceCounter();
      g_dbg_begin_ms = (dbg_t1 - dbg_t0) * 1000.f / (float)SDL_GetPerformanceFrequency();
#endif

      // SDL fires SDL_MOUSEMOTION for every mouse move, so had_event already covers hover updates.
      // WantCaptureMouse would force a frame whenever the mouse is anywhere over the app — even
      // stationary — burning 323ms for nothing. Only keep keyboard to sustain cursor blink / IME.
      const bool imgui_active = ImGui::GetIO().WantCaptureKeyboard;
      // Keep rendering while the history indicator fade animation is running.
      const bool animation_active =
          !history_indicator_.text.empty() && history_indicator_.until > ImGui::GetTime();
      const bool terminal_active = terminal_visible_;

      dirty_ = dirty_ || had_event || state_dirty_ || layout_dirty_ || g_drawings_dirty || animation_active || terminal_active;

      if(dirty_ || imgui_active || keep_alive_frames > 0)
      {
        keep_alive_frames = (had_event || imgui_active || animation_active || terminal_active) ? 2
                                                                                               : keep_alive_frames - 1;
        frame_ui();
        const bool sidebar_thumbnail_work_deferred =
            MarkdownView::sidebar_thumbnail_work_deferred();
#ifdef NOTEPP_DEBUG_UI
        const Uint64 dbg_t2 = SDL_GetPerformanceCounter();
#endif
        frame_end();
        dirty_ = imgui_active || animation_active || terminal_active || sidebar_thumbnail_work_deferred;
        limit_frame_rate();
#ifdef NOTEPP_DEBUG_UI
        const Uint64 dbg_t3 = SDL_GetPerformanceCounter();
        const float dbg_freq = (float)SDL_GetPerformanceFrequency();
        g_dbg_ui_ms = (dbg_t2 - dbg_t1) * 1000.f / dbg_freq;
        g_dbg_end_ms = (dbg_t3 - dbg_t2) * 1000.f / dbg_freq;
#endif
      }
      else
      {
        // Nothing to render — discard the ImGui frame and yield the CPU.
        ImGui::EndFrame();
        // UpdatePlatformWindows must be called after every EndFrame when
        // viewports are enabled, or ImGui fires a sanity-check assertion.
        if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
          ImGui::UpdatePlatformWindows();
        // Block until an event arrives. The file-watch timer may wake us to scan,
        // but unchanged static notes do not redraw just because time passed.
        SDL_Event ev;
        if(SDL_WaitEvent(&ev) == 1)
          SDL_PushEvent(&ev);
      }
    }

    last_save_succeeded_ = close_persisted_ && close_save_succeeded_;
    shutdown();
    return 0;
  }
  catch(const std::exception &e)
  {
    LOG_ERROR("Fatal: ", e.what());
    last_save_succeeded_ = false;
    shutdown();
    return 1;
  }
}
void App::shutdown()
{
  command_ipc_server_.close();
  command_api_.reset();
  if(git_sync_future_.valid())
  {
    try
    {
      const GitAsyncResult result = git_sync_future_.get();
      record_git_status(result.operation.status);
    }
    catch(const std::exception &error)
    {
      record_git_status(
          notepp::git_sync::exception_result("Stopping Git Sync", error.what()).status);
    }
    catch(...)
    {
      record_git_status(notepp::git_sync::exception_result("Stopping Git Sync").status);
    }
    git_sync_in_progress_ = false;
    git_operation_kind_ = GitOperationKind::none;
    atomic_file::set_shared_writes_suspended(false);
  }
  atomic_file::set_shared_writes_suspended(false);
  MarkdownWidgets::set_command_action_handler({});
  MarkdownWidgets::set_terminal_command_handler({});
  terminal_.stop();

  clear_toolbar_icon_cache();
  MarkdownView::shutdown_sidebar_thumbnail_cache();

  if(file_watch_timer_ != 0)
  {
    SDL_RemoveTimer(file_watch_timer_);
    file_watch_timer_ = 0;
  }

  if(ImGui::GetCurrentContext())
  {
    NoteUi::destroy_icon_shader();
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
  bool first_run = false;
  discard_pending_text_history();
  history_.clear();
  folders_.clear();
  pending_fs_delete_paths_.clear();
  active_folder_idx_ = 0;
  active_note_idx_ = 0;
  folder_overview_mode_ = false;
  layout_locked_ = false;
  detached_note_windows_enabled_ = false;
  dockers_enabled_ = false;

  const std::string notes_top_level = project_notes_top_level(config_);
  const notepp::project_paths::ProjectPaths project_paths(config_.projectRoot);
  bool uuid_migrated = false;
  bool paths_migrated = false;
  bool fingerprints_migrated = false;
  bool path_migration_failed = false;
  index_schema_version_ = notepp::note_index::current_schema_version;
  index_paths_portable_ = true;
  index_writable_ = true;
  index_source_document_.clear();
  const auto loaded_index = g_project_files.load(index_file_);
  record_project_read(index_file_, loaded_index, true);
  first_run = loaded_index && !loaded_index.snapshot.existed;
  Json index_document;
  notepp::note_index::DocumentState index_state = notepp::note_index::DocumentState::missing;
  if(!loaded_index)
  {
    index_writable_ = false;
    (void)record_project_save_error(index_file_, loaded_index.message);
    LOG_ERROR(loaded_index.message);
  }
  else
  {
    if(loaded_index.snapshot.existed)
      index_document = Json::parse(loaded_index.snapshot.content, nullptr, false);
    index_state = notepp::note_index::validate_document(
        index_document, loaded_index.snapshot.existed);
    if(index_state == notepp::note_index::DocumentState::malformed)
    {
      index_writable_ = false;
      (void)record_project_save_error(
          index_file_, "notes index is malformed; canonical reconstruction is blocked");
    }
    else if(index_state == notepp::note_index::DocumentState::future_schema)
    {
      index_writable_ = false;
      (void)record_project_save_error(
          index_file_, "notes index uses a newer unsupported schema; opened read-only");
    }
  }
  if(index_writable_ && loaded_index && loaded_index.snapshot.existed)
  {
    const auto decoded_index = notepp::note_index::decode_document(index_document);
    if(!decoded_index)
    {
      index_writable_ = false;
      (void)record_project_save_error(
          index_file_, "notes index could not be decoded after validation; opened read-only");
    }
    else
    {
      index_source_document_ = loaded_index.snapshot.content;
      index_schema_version_ = decoded_index->schema_version;
      index_paths_portable_ = index_schema_version_ >= 2;
      active_folder_idx_ = decoded_index->active_folder;
      active_note_idx_ = decoded_index->active_note;
      folder_overview_mode_ = decoded_index->folder_view;
      layout_locked_ = decoded_index->layout_locked;
      detached_note_windows_enabled_ = decoded_index->detached_note_windows;
      dockers_enabled_ = decoded_index->dockers_enabled;
      if(!project_language_explicit_ && !decoded_index->language.empty())
      {
        if(Lang::set_language(decoded_index->language))
        {
          project_language_explicit_ = true;
          project_settings_dirty_ = true;
        }
      }

      for(const auto &folder_record : decoded_index->folders)
      {
        FolderMeta folder;
        folder.name = folder_record.name;
        folder.layout_locked = folder_record.layout_locked;
        folder.detached_note_windows = folder_record.detached_note_windows;
        folder.dockers_enabled = folder_record.dockers_enabled;
        folder.drawings_visible = folder_record.drawings_visible;
        folder.grid_visible = folder_record.grid_visible;

        for(const auto &note_record : folder_record.notes)
        {
          NoteMeta note;
          note.id = note_record.id;
          if(note.id.empty())
          {
            note.id = generate_uuid();
            uuid_migrated = true;
          }
          note.title = note_record.title;
          note.content_fingerprint = note_record.content_fingerprint;
          if(auto resolved = resolve_project_owned_path(
                 project_paths, note_record.path, index_schema_version_, notes_top_level))
          {
            note.path = resolved->string();
            paths_migrated = paths_migrated || index_schema_version_ < 2;
          }
          else
          {
            const std::filesystem::path expected_path = make_note_path(folder.name, note.title);
            std::error_code expected_error;
            if(index_schema_version_ < 2 &&
               std::filesystem::exists(expected_path, expected_error) && !expected_error)
            {
              note.path = expected_path.string();
              paths_migrated = true;
            }
            else
            {
              note.path.clear();
              note.unresolved_stored_path = note_record.path;
              path_migration_failed = true;
              LOG_ERROR("Cannot migrate note path '", note_record.path,
                        "' in folder '", folder.name,
                        "'; preserving legacy index schema");
            }
          }
          note.pos_x = static_cast<float>(note_record.x);
          note.pos_y = static_cast<float>(note_record.y);
          note.width = static_cast<float>(note_record.width);
          note.height = static_cast<float>(note_record.height);
          note.has_layout = note_record.has_layout;
          note.hidden = note_record.hidden;
          note.always_on_top = note_record.always_on_top;
          note.dock_id = static_cast<ImGuiID>(note_record.dock_id);
          note.use_custom_color = note_record.use_custom_color;
          note.color_r = static_cast<float>(note_record.color_r) / 255.0F;
          note.color_g = static_cast<float>(note_record.color_g) / 255.0F;
          note.color_b = static_cast<float>(note_record.color_b) / 255.0F;
          if(!note_record.font_path.empty())
          {
            if(auto resolved_font = resolve_project_owned_path(
                   project_paths, note_record.font_path, index_schema_version_, notes_top_level))
            {
              note.font_path = resolved_font->string();
              paths_migrated = paths_migrated || index_schema_version_ < 2;
            }
            else
            {
              std::optional<notepp::project_paths::LegacyPathResult> migrated_font;
              if(index_schema_version_ < 2)
              {
                if(auto encoded_parent = project_paths.encode(config_.dataPath / folder.name))
                {
                  if(auto result = project_paths.migrate_legacy_child(
                         note_record.font_path, *encoded_parent, notes_top_level))
                    migrated_font = *result;
                }
              }
              if(migrated_font)
              {
                note.font_path = migrated_font->absolute_path.string();
                paths_migrated = true;
              }
              else
              {
                note.font_path.clear();
                note.unresolved_stored_font_path = note_record.font_path;
                path_migration_failed = true;
                LOG_ERROR("Cannot migrate font path '", note_record.font_path,
                          "'; preserving it as non-I/O metadata");
              }
            }
          }
          note.font_size = note_record.font_size;
          if(note.path.empty() && note.unresolved_stored_path.empty())
            note.path = make_note_path(folder.name, note.title);
          folder.notes.push_back(std::move(note));
        }

        for(const std::string &stored_image : folder_record.images)
        {
          if(stored_image.empty()) continue;
          if(auto resolved_image = resolve_project_owned_path(
                 project_paths, stored_image, index_schema_version_, notes_top_level))
          {
            folder.images.push_back(resolved_image->string());
            paths_migrated = paths_migrated || index_schema_version_ < 2;
          }
          else
          {
            folder.unresolved_stored_images.push_back(stored_image);
            path_migration_failed = true;
            LOG_ERROR("Cannot migrate image path in folder '", folder.name,
                      "'; preserving legacy index schema");
          }
        }
        folders_.push_back(std::move(folder));
      }
    }
  }
  else
  {
    // Migration from legacy current_note_path.txt
    std::string migrated_path;
    std::ifstream legacy(legacy_state_meta_file_);
    if(legacy)
    {
      std::string p;
      if(std::getline(legacy, p) && !p.empty()) migrated_path = p;
    }

    if(!migrated_path.empty())
    {
      FolderMeta f;
      f.name = "General";
      NoteMeta n;
      n.id = generate_uuid();
      n.path = migrated_path;
      const std::filesystem::path fp(migrated_path);
      n.title = fp.stem().empty() ? "Note" : fp.stem().string();
      f.notes.push_back(std::move(n));
      folders_.push_back(std::move(f));
    }
  }

  const int loaded_index_schema = index_schema_version_;
  index_schema_version_ = notepp::note_index::schema_after_path_migration(
      loaded_index_schema, path_migration_failed);
  index_paths_portable_ = index_schema_version_ >= 2;
  paths_migrated = paths_migrated || index_schema_version_ != loaded_index_schema;

  if(index_writable_)
  {
    for(auto &folder : folders_)
    {
      for(auto &note : folder.notes)
      {
        if(note.path.empty() ||
           notepp::note_index::strong_content_fingerprint(note.content_fingerprint))
          continue;
        std::error_code exists_error;
        if(!std::filesystem::exists(note.path, exists_error) || exists_error) continue;
        const auto loaded_note = g_project_files.ensure_loaded(note.path);
        record_project_read(note.path, loaded_note);
        if(!loaded_note || !loaded_note.snapshot.existed) continue;
        note.content_fingerprint = notepp::note_index::content_fingerprint(
            loaded_note.snapshot.content);
        fingerprints_migrated = true;
      }
    }
    sync_project_files();
    ensure_default_index();
  }

  {
    bool has_any_note = false;
    for(const auto &f : folders_)
      if(!f.notes.empty())
      {
        has_any_note = true;
        break;
      }
    if(first_run && index_writable_ && !has_any_note)
      open_or_create_readme();
  }

  load_workspace();
  normalize_active_indices();
  apply_folder_settings(active_folder_idx_);
  load_drawings_state();
  load_note_clipboard(config_);
  load_note_content_for_active();
  load_profiles();
  capture_to_active_profile();
  const bool workspace_saved = save_workspace();
  const bool profiles_saved = save_profiles();
  workspace_migration_complete_ =
      legacy_workspace_migration_complete_ && workspace_saved && profiles_saved;
  if(!workspace_saved) LOG_ERROR("Cannot save local workspace state");
  if(!profiles_saved) LOG_ERROR("Cannot save local layout profiles");
  if(uuid_migrated || paths_migrated || fingerprints_migrated) save_index();

}

bool App::save_state()
{
  if(atomic_file::shared_writes_suspended()) return false;
  g_project_save_batch.clear();
  const bool command_mutation_pending = !pending_command_mutations_.empty();
  bool all_saved = true;
  const bool record_text_change =
      !history_replay_in_progress_ &&
      !state_file_path_.empty() &&
      !deferred_text_snapshot_before_.empty() &&
      deferred_text_snapshot_before_ != markdown_text_;
  const std::string before_text = std::move(deferred_text_snapshot_before_);
  deferred_text_snapshot_before_.clear();

  if(!state_file_path_.empty())
  {
    if(!g_project_persistence_guard.may_write(state_file_path_) && !state_dirty_)
    {
      // Keep the read error visible, but do not create an empty recovery note
      // until the failed-load placeholder was actually edited.
      all_saved = false;
    }
    else if(save_project_text(state_file_path_, markdown_text_))
    {
      MarkdownSupport::notify_document_saved(state_file_path_);
      update_note_cache(state_file_path_, markdown_text_);
      if(has_active_note())
        folders_[static_cast<std::size_t>(active_folder_idx_)]
            .notes[static_cast<std::size_t>(active_note_idx_)]
            .content_fingerprint = notepp::note_index::content_fingerprint(markdown_text_);
    }
    else
      all_saved = false;
  }
  capture_to_active_profile();
  const bool workspace_saved = save_workspace();
  const bool profiles_saved = save_profiles();
  workspace_migration_complete_ =
      legacy_workspace_migration_complete_ && workspace_saved && profiles_saved;
  if(!workspace_saved) LOG_ERROR("Cannot save local workspace state");
  if(g_clipboard_dirty && !save_note_clipboard())
    LOG_ERROR("Cannot save local note clipboard");
  if(!profiles_saved) LOG_ERROR("Cannot save local layout profiles");
  // Command handlers persist index mutations immediately, but their in-memory
  // reload is deferred until frame_ui() finishes rendering. Do not serialize
  // stale folders_ over those changes; markdown and workspace saves above are
  // still safe to perform for this frame.
  if(!command_mutation_pending && !save_index()) all_saved = false;
  if(g_drawings_dirty && !save_drawings_state()) all_saved = false;
  if(!MarkdownSupport::flush_preview_state())
  {
    g_last_persistence_error = MarkdownSupport::last_persistence_error();
    LOG_ERROR("Cannot save local markdown preview state: ", g_last_persistence_error);
  }
  if(const std::string widget_error = MarkdownWidgets::last_persistence_error();
     !widget_error.empty())
  {
    all_saved = false;
    g_last_persistence_error = widget_error;
    LOG_ERROR("Cannot save widget globals: ", widget_error);
  }

  if(record_text_change && all_saved)
    record_text_history_action("Edit text", before_text, markdown_text_);
  else if(record_text_change)
    deferred_text_snapshot_before_ = before_text;

  last_save_succeeded_ = all_saved &&
                         g_project_save_batch.canonical_saves_succeeded() &&
                         g_unresolved_persistence_errors.empty();
  return last_save_succeeded_;
}

void App::sync_active_folder_settings()
{
  if(active_folder_idx_ < 0 || active_folder_idx_ >= (int)folders_.size()) return;
  FolderMeta &f = folders_[(size_t)active_folder_idx_];
  f.layout_locked = layout_locked_;
  f.detached_note_windows = detached_note_windows_enabled_;
  f.dockers_enabled = dockers_enabled_;
  f.drawings_visible = drawings_visible_;
  f.grid_visible = grid_visible_;
}

void App::apply_folder_settings(int folder_idx)
{
  if(folder_idx < 0 || folder_idx >= (int)folders_.size()) return;
  const FolderMeta &f = folders_[(size_t)folder_idx];
  const bool was_docking_enabled = dockers_enabled_;
  layout_locked_ = f.layout_locked;
  detached_note_windows_enabled_ = f.detached_note_windows;
  dockers_enabled_ = f.dockers_enabled;
  drawings_visible_ = f.drawings_visible;
  grid_visible_ = f.grid_visible;
  set_dockers_enabled(dockers_enabled_);
  set_detached_note_windows_enabled(detached_note_windows_enabled_);
  if(dockers_enabled_ && !was_docking_enabled)
    force_note_layout_restore_ = true;
  if(!drawings_visible_)
    request_cancel_draw_tools_ = true;
}

bool App::save_index()
{
  if(atomic_file::shared_writes_suspended()) return false;
  if(!index_writable_)
    return record_project_save_error(index_file_,
                                     "notes index is read-only until its load error is resolved");
  const std::string notes_top_level = project_notes_top_level(config_);
  const notepp::project_paths::ProjectPaths project_paths(config_.projectRoot);
  std::unordered_map<std::string, std::optional<std::string>> path_cache;
  auto stored_path = [&](const std::string &runtime_path,
                         std::string_view expected_top_level,
                         const std::string &folder_name = {}) -> std::optional<std::string> {
    if(runtime_path.empty()) return std::string{};
    if(index_schema_version_ < 2) return runtime_path;

    std::filesystem::path candidate(runtime_path);
    if(candidate.is_relative() && !folder_name.empty())
      candidate = config_.dataPath / folder_name / candidate;
    const std::string cache_key = candidate.lexically_normal().string() + "\n" +
                                  std::string(expected_top_level);
    if(const auto found = path_cache.find(cache_key); found != path_cache.end())
      return found->second;
    auto encoded = portable_project_path(project_paths, candidate, expected_top_level);
    path_cache.emplace(cache_key, encoded);
    return encoded;
  };

  for(const auto &folder : folders_)
  {
    for(const auto &note : folder.notes)
    {
      if(note.unresolved_stored_path.empty() && !stored_path(note.path, notes_top_level))
      {
        return record_project_save_error(
            index_file_, "refusing to save non-portable note path: " + note.path);
      }
      if(note.unresolved_stored_font_path.empty() && !note.font_path.empty() &&
         !stored_path(note.font_path, notes_top_level, folder.name))
      {
        return record_project_save_error(
            index_file_, "refusing to save non-portable font path: " + note.font_path);
      }
    }
    for(const auto &image : folder.images)
    {
      if(!stored_path(image, notes_top_level))
      {
        return record_project_save_error(
            index_file_, "refusing to save non-portable image path: " + image);
      }
    }
  }

  for(auto &folder : folders_)
  {
    for(auto &note : folder.notes)
    {
      if(note.path.empty() ||
         notepp::note_index::strong_content_fingerprint(note.content_fingerprint))
        continue;
      const auto snapshot = g_project_files.ensure_loaded(note.path);
      if(snapshot && snapshot.snapshot.existed)
        note.content_fingerprint = notepp::note_index::content_fingerprint(
            snapshot.snapshot.content);
    }
  }

  Json source_root = Json::parse(index_source_document_, nullptr, false);
  if(!source_root.is_object()) source_root = Json::object();
  Json root = Json::object();

  root["schemaVersion"] = index_schema_version_;
  root["language"] = Lang::current_language_code();

  Json persisted_folders = Json::array();
  for(std::size_t fi = 0; fi < folders_.size(); ++fi)
  {
    const auto &folder = folders_[fi];
    Json persisted_folder = Json::object();
    persisted_folder["name"] = folder.name;
    Json persisted_notes = Json::array();
    for(std::size_t ni = 0; ni < folder.notes.size(); ++ni)
    {
      const auto &note = folder.notes[ni];
      Json persisted_note = Json::object();
      persisted_note["id"] = note.id;
      persisted_note["title"] = note.title;
      persisted_note["path"] = note.unresolved_stored_path.empty()
                                   ? *stored_path(note.path, notes_top_level)
                                   : note.unresolved_stored_path;
      if(!note.content_fingerprint.empty())
        persisted_note["content_fingerprint"] = note.content_fingerprint;
      persisted_note["use_custom_color"] = note.use_custom_color;
      persisted_note["color_r"] =
          (int)std::lround(std::clamp(note.color_r, 0.0f, 1.0f) * 255.0f);
      persisted_note["color_g"] =
          (int)std::lround(std::clamp(note.color_g, 0.0f, 1.0f) * 255.0f);
      persisted_note["color_b"] =
          (int)std::lround(std::clamp(note.color_b, 0.0f, 1.0f) * 255.0f);
      if(!note.unresolved_stored_font_path.empty())
        persisted_note["font_path"] = note.unresolved_stored_font_path;
      else if(!note.font_path.empty())
        persisted_note["font_path"] = *stored_path(note.font_path, notes_top_level, folder.name);
      else
        persisted_note.erase("font_path");
      if(note.font_size > 0.0f)
        persisted_note["font_size"] = note.font_size;
      else
        persisted_note.erase("font_size");
      persisted_notes.push_back(std::move(persisted_note));
    }
    persisted_folder["notes"] = std::move(persisted_notes);

    Json persisted_images = Json::array();
    for(const auto &image : folder.images)
      persisted_images.push_back(*stored_path(image, notes_top_level));
    for(const auto &unresolved_image : folder.unresolved_stored_images)
      persisted_images.push_back(unresolved_image);
    persisted_folder["images"] = std::move(persisted_images);
    persisted_folders.push_back(std::move(persisted_folder));
  }
  root["folders"] = std::move(persisted_folders);
  root = notepp::note_index::merge_unknown_fields(source_root, root);
  if(workspace_migration_complete_)
  {
    for(const char *key : {"active_folder", "active_note", "folder_view", "layout_locked",
                           "detached_note_windows", "dockers_enabled"})
      root.erase(key);
    for(Json &folder : root["folders"])
    {
      for(const char *key : {"layout_locked", "detached_note_windows", "dockers_enabled",
                             "drawings_visible", "grid_visible"})
        folder.erase(key);
      for(Json &note : folder["notes"])
        for(const char *key : {"x", "y", "w", "h", "has_layout", "hidden",
                               "always_on_top", "dock_id"})
          note.erase(key);
    }
  }

  std::string serialized = root.dump(2);
  serialized.push_back('\n');
  if(!save_project_text(index_file_, serialized)) return false;
  index_source_document_ = std::move(serialized);
  return true;
}

void App::load_profiles()
{
  layout_profiles_.clear();
  active_profile_id_.clear();
  maximized_profile_id_.clear();
  reduced_profile_id_.clear();
  profiles_source_document_.clear();
  profiles_writable_ = true;

  const auto loaded_profiles = atomic_file::read_text(profiles_file_);
  if(!loaded_profiles)
  {
    profiles_writable_ = false;
    LOG_ERROR(loaded_profiles.message);
    return;
  }
  Json profile_document;
  if(loaded_profiles.snapshot.existed)
    profile_document = Json::parse(loaded_profiles.snapshot.content, nullptr, false);
  const auto profile_state = notepp::layout_profile_state::validate_document(
      profile_document, loaded_profiles.snapshot.existed);
  if(profile_state == notepp::layout_profile_state::DocumentState::malformed ||
     profile_state == notepp::layout_profile_state::DocumentState::future_schema)
  {
    profiles_writable_ = false;
    const std::string message =
        profile_state == notepp::layout_profile_state::DocumentState::future_schema
            ? "layout profiles use a newer unsupported schema; opened read-only"
            : "layout profiles are malformed; canonical reconstruction is blocked";
    LOG_ERROR(message);
    return;
  }
  if(!loaded_profiles.snapshot.existed)
  {
    // First run: create the single default "Default" profile (maximized)
    LayoutProfile p;
    p.id = generate_uuid();
    p.name = "Default";
    p.window_maximized = true;
    if(window_)
    {
      SDL_GetWindowSize(window_, &p.window_w, &p.window_h);
      SDL_GetWindowPosition(window_, &p.window_x, &p.window_y);
    }
    layout_profiles_.push_back(std::move(p));
    maximized_profile_id_ = layout_profiles_.back().id;
    active_profile_id_ = maximized_profile_id_;
    capture_to_active_profile();
    save_profiles();
    return;
  }

  const auto decoded_profiles =
      notepp::layout_profile_state::decode_document(profile_document);
  if(!decoded_profiles)
  {
    profiles_writable_ = false;
    LOG_ERROR("layout profiles could not be decoded after validation; opened read-only");
    return;
  }

  profiles_source_document_ = loaded_profiles.snapshot.content;
  active_profile_id_ = decoded_profiles->active_profile_id;
  maximized_profile_id_ = decoded_profiles->maximized_profile_id;
  reduced_profile_id_ = decoded_profiles->reduced_profile_id;
  const auto has_persisted_always_on_top = [&profile_document](const std::string &profile_id,
                                                               const std::string &note_id) {
    const auto profiles = profile_document.find("profiles");
    if(profiles == profile_document.end() || !profiles->is_array()) return false;
    for(const Json &profile : *profiles)
    {
      if(!profile.is_object() || profile.value("id", std::string{}) != profile_id) continue;
      const auto layouts = profile.find("note_layouts");
      if(layouts == profile.end() || !layouts->is_array()) return false;
      for(const Json &layout : *layouts)
        if(layout.is_object() && layout.value("note_id", std::string{}) == note_id)
          return layout.contains("always_on_top");
      return false;
    }
    return false;
  };
  const auto legacy_always_on_top = [this](const std::string &note_id) {
    for(const FolderMeta &folder : folders_)
      for(const NoteMeta &note : folder.notes)
        if(note.id == note_id) return note.always_on_top;
    return false;
  };
  for(const auto &profile_record : decoded_profiles->profiles)
  {
    LayoutProfile profile;
    profile.id = profile_record.id;
    profile.name = profile_record.name;
    profile.window_maximized = profile_record.window_maximized;
    profile.window_x = profile_record.window_x;
    profile.window_y = profile_record.window_y;
    profile.window_w = profile_record.window_w;
    profile.window_h = profile_record.window_h;
    for(const auto &[note_id, layout_record] : profile_record.note_layouts)
    {
      NoteLayoutData layout;
      layout.pos_x = static_cast<float>(layout_record.x);
      layout.pos_y = static_cast<float>(layout_record.y);
      layout.width = static_cast<float>(layout_record.width);
      layout.height = static_cast<float>(layout_record.height);
      layout.hidden = layout_record.hidden;
      layout.always_on_top = has_persisted_always_on_top(profile_record.id, note_id)
                                 ? layout_record.always_on_top
                                 : legacy_always_on_top(note_id);
      layout.has_layout = layout_record.has_layout;
      layout.dock_id = static_cast<ImGuiID>(layout_record.dock_id);
      profile.note_layouts.emplace(note_id, layout);
    }
    layout_profiles_.push_back(std::move(profile));
  }

  if(layout_profiles_.empty())
  {
    // A valid legacy document can contain no profiles. Create one before the
    // caller captures and persists the legacy note layouts; otherwise the
    // project index could be cleaned without a local replacement.
    LayoutProfile profile;
    profile.id = generate_uuid();
    profile.name = "Default";
    profile.window_maximized = true;
    if(window_)
    {
      SDL_GetWindowSize(window_, &profile.window_w, &profile.window_h);
      SDL_GetWindowPosition(window_, &profile.window_x, &profile.window_y);
    }
    layout_profiles_.push_back(std::move(profile));
    active_profile_id_ = layout_profiles_.back().id;
    maximized_profile_id_ = active_profile_id_;
  }

  // Restore last active profile (apply its window state so the window
  // opens exactly as the user left it).
  const LayoutProfile *last = find_active_profile();
  if(last)
    apply_profile(*last, true, false);
  else
  {
    // Fallback for an invalid or absent saved ID: first match the current
    // window, then select the first profile so layout capture always has an
    // owner before legacy index fields are removed.
    const LayoutProfile *match = find_matching_profile();
    if(match)
    {
      active_profile_id_ = match->id;
      apply_profile(*match, false, false);
    }
    else
    {
      active_profile_id_ = layout_profiles_.front().id;
    }
  }
}

bool App::save_profiles()
{
  if(atomic_file::shared_writes_suspended()) return false;
  if(!profiles_writable_)
  {
    LOG_ERROR("layout profiles are read-only until their load error is resolved");
    return false;
  }

  Json root = Json::object();
  root["schemaVersion"] = notepp::layout_profile_state::current_schema_version;
  root["active_profile_id"] = active_profile_id_;
  root["maximized_profile_id"] = maximized_profile_id_;
  root["reduced_profile_id"] = reduced_profile_id_;
  root["profiles"] = Json::array();
  for(const auto &profile : layout_profiles_)
  {
    Json value = {{"id", profile.id},
                  {"name", profile.name},
                  {"window_maximized", profile.window_maximized},
                  {"window_x", profile.window_x},
                  {"window_y", profile.window_y},
                  {"window_w", profile.window_w},
                  {"window_h", profile.window_h},
                  {"note_layouts", Json::array()}};
    for(const auto &[note_id, layout] : profile.note_layouts)
      value["note_layouts"].push_back(
          {{"note_id", note_id},
           {"x", (int)std::lround(layout.pos_x)},
           {"y", (int)std::lround(layout.pos_y)},
           {"w", (int)std::lround(layout.width)},
           {"h", (int)std::lround(layout.height)},
           {"hidden", layout.hidden},
           {"always_on_top", layout.always_on_top},
           {"has_layout", layout.has_layout},
           {"dock_id", layout.dock_id}});
    root["profiles"].push_back(std::move(value));
  }

  const Json source = Json::parse(profiles_source_document_, nullptr, false);
  root = notepp::layout_profile_state::merge_unknown_fields(source, root);
  std::string serialized = root.dump(2);
  serialized.push_back('\n');
  if(!save_workspace_text(profiles_file_, serialized)) return false;
  profiles_source_document_ = std::move(serialized);
  return true;
}

void App::capture_to_active_profile()
{
  if(active_profile_id_.empty()) return;
  auto it = std::find_if(layout_profiles_.begin(), layout_profiles_.end(),
                         [&](const LayoutProfile &p) { return p.id == active_profile_id_; });
  if(it == layout_profiles_.end()) return;

  LayoutProfile &profile = *it;

  if(window_)
  {
    profile.window_maximized = is_window_covering_display();
    // Store monitor bounds for maximized profiles so multi-monitor restores are deterministic.
    if(profile.window_maximized)
    {
      const int display_index = display_index_for_window(window_);
      SDL_Rect bounds{};
      if(display_index >= 0 && get_display_bounds(display_index, bounds))
      {
        profile.window_x = bounds.x;
        profile.window_y = bounds.y;
        profile.window_w = bounds.w;
        profile.window_h = bounds.h;
      }
    }
    else
    {
      SDL_GetWindowPosition(window_, &profile.window_x, &profile.window_y);
      SDL_GetWindowSize(window_, &profile.window_w, &profile.window_h);
    }
  }

  profile.note_layouts.clear();
  for(const auto &f : folders_)
  {
    for(const auto &n : f.notes)
    {
      if(n.id.empty()) continue;
      NoteLayoutData nd;
      nd.pos_x = n.pos_x;
      nd.pos_y = n.pos_y;
      nd.width = n.width;
      nd.height = n.height;
      nd.hidden = n.hidden;
      nd.always_on_top = n.always_on_top;
      nd.has_layout = n.has_layout;
      nd.dock_id = n.dock_id;
      profile.note_layouts[n.id] = nd;
    }
  }
}

void App::apply_profile(const LayoutProfile &profile, bool apply_window_state, bool mark_dirty)
{
  for(auto &f : folders_)
  {
    for(auto &n : f.notes)
    {
      auto it = profile.note_layouts.find(n.id);
      if(it != profile.note_layouts.end())
      {
        const NoteLayoutData &nd = it->second;
        n.pos_x = nd.pos_x;
        n.pos_y = nd.pos_y;
        n.width = nd.width;
        n.height = nd.height;
        n.hidden = nd.hidden;
        n.always_on_top = nd.always_on_top;
        n.has_layout = nd.has_layout;
        n.dock_id = nd.dock_id;
      }
    }
  }
  force_note_layout_restore_ = true;
  if(mark_dirty) layout_dirty_ = true;

  if(apply_window_state && window_)
  {
    if(profile.window_maximized)
    {
      apply_borderless_maximized_window(
          window_, profile.window_x, profile.window_y, profile.window_w, profile.window_h);
    }
    else
    {
      apply_borderless_window_rect(
          window_, profile.window_x, profile.window_y, profile.window_w, profile.window_h);
    }
  }
}

App::LayoutProfile *App::find_active_profile()
{
  if(active_profile_id_.empty()) return nullptr;
  auto it = std::find_if(layout_profiles_.begin(), layout_profiles_.end(),
                         [&](const LayoutProfile &p) { return p.id == active_profile_id_; });
  return it != layout_profiles_.end() ? &*it : nullptr;
}

std::string App::create_profile(const std::string &name, bool maximized,
                                int x, int y, int w, int h)
{
  push_profile_snapshot();
  LayoutProfile p;
  p.id = generate_uuid();
  p.name = name;
  p.window_maximized = maximized;
  p.window_x = x;
  p.window_y = y;
  p.window_w = w;
  p.window_h = h;
  layout_profiles_.push_back(std::move(p));
  active_profile_id_ = layout_profiles_.back().id;
  apply_profile(layout_profiles_.back(), true);
  capture_to_active_profile();
  window_profile_check_pending_ = false;
  window_profile_check_delay_ = 0;
  save_profiles();
  save_index();
  return active_profile_id_;
}

void App::push_profile_snapshot()
{
  // Re-use the workspace snapshot mechanism so profile changes join the undo stack
  const std::string before = capture_workspace_snapshot();
  record_workspace_history_action("Profile change", before);
}

void App::delete_profile(const std::string &id)
{
  auto it = std::find_if(layout_profiles_.begin(), layout_profiles_.end(),
                         [&](const LayoutProfile &p) { return p.id == id; });
  if(it == layout_profiles_.end()) return;
  const bool was_active = (active_profile_id_ == id);
  const bool was_maximized = (maximized_profile_id_ == id);
  const bool was_reduced = (reduced_profile_id_ == id);
  layout_profiles_.erase(it);
  if(was_active) active_profile_id_.clear();
  if(was_maximized) maximized_profile_id_.clear();
  if(was_reduced) reduced_profile_id_.clear();
  save_profiles();
}

bool App::is_window_covering_display() const
{
  if(!window_) return false;
  const Uint32 flags = SDL_GetWindowFlags(window_);
  if((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) return true;
  if((flags & SDL_WINDOW_MAXIMIZED) == SDL_WINDOW_MAXIMIZED) return true;

  int x = 0, y = 0, w = 0, h = 0;
  SDL_GetWindowPosition(window_, &x, &y);
  SDL_GetWindowSize(window_, &w, &h);

  const int display_index = std::max(0, SDL_GetWindowDisplayIndex(window_));
  SDL_Rect bounds{};
  if(!get_display_bounds(display_index, bounds)) return false;

  constexpr int kTolerance = 10;
  return std::abs(x - bounds.x) <= kTolerance &&
         std::abs(y - bounds.y) <= kTolerance &&
         std::abs(w - bounds.w) <= kTolerance &&
         std::abs(h - bounds.h) <= kTolerance;
}

const App::LayoutProfile *App::find_matching_profile() const
{
  if(!window_) return nullptr;
  const bool is_maximized = is_window_covering_display();
  int cur_w = 0, cur_h = 0;
  int cur_x = 0, cur_y = 0;
  SDL_GetWindowSize(window_, &cur_w, &cur_h);
  SDL_GetWindowPosition(window_, &cur_x, &cur_y);

  for(const auto &p : layout_profiles_)
  {
    if(p.window_maximized != is_maximized) continue;
    constexpr int kTolerance = 10;
    if(is_maximized)
    {
      SDL_Rect preferred{p.window_x, p.window_y, std::max(1, p.window_w), std::max(1, p.window_h)};
      const int display_index = best_display_index_for_rect(preferred);
      SDL_Rect profile_bounds{};
      if(display_index >= 0 && get_display_bounds(display_index, profile_bounds))
      {
        if(std::abs(cur_x - profile_bounds.x) > kTolerance) continue;
        if(std::abs(cur_y - profile_bounds.y) > kTolerance) continue;
        if(std::abs(cur_w - profile_bounds.w) > kTolerance) continue;
        if(std::abs(cur_h - profile_bounds.h) > kTolerance) continue;
      }
    }
    else
    {
      int profile_x = p.window_x;
      int profile_y = p.window_y;
      int profile_w = p.window_w;
      int profile_h = p.window_h;
      sanitize_window_rect_for_displays(profile_x, profile_y, profile_w, profile_h);
      if(std::abs(cur_x - profile_x) > kTolerance) continue;
      if(std::abs(cur_y - profile_y) > kTolerance) continue;
      if(std::abs(cur_w - profile_w) > kTolerance) continue;
      if(std::abs(cur_h - profile_h) > kTolerance) continue;
    }
    return &p;
  }
  return nullptr;
}

void App::do_window_profile_switch()
{
  if(git_sync_in_progress_ || close_requested_)
  {
    window_profile_check_pending_ = true;
    return;
  }
  const LayoutProfile *matching = find_matching_profile();
  if(matching)
  {
    if(matching->id != active_profile_id_)
    {
      capture_to_active_profile();
      active_profile_id_ = matching->id;
      apply_profile(*matching, false);
      save_profiles();
      save_index();
    }
  }
  else
  {
    if(!active_profile_id_.empty())
    {
      capture_to_active_profile();
      save_profiles();
      active_profile_id_.clear();
    }
  }
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
  }
  const std::string n = sanitize_note_filename(note_title);
  if(f.empty())
    return (config_.dataPath / (n + ".md")).string();
  std::filesystem::path dir = config_.dataPath / f;
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
  normalize_active_indices();
}

void App::open_or_create_readme()
{
  for(int fi = 0; fi < (int)folders_.size(); ++fi)
  {
    const FolderMeta &f = folders_[(size_t)fi];
    for(int ni = 0; ni < (int)f.notes.size(); ++ni)
    {
      std::string lower = f.notes[(size_t)ni].title;
      for(auto &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if(lower == "demo")
      {
        state_dirty_ = true;
        active_folder_idx_ = fi;
        active_note_idx_ = ni;
        editing_mode_ = false;
        request_exit_edit_mode_ = false;
        load_note_content_for_active();
        save_index();
        return;
      }
    }
  }

  // Create demo note in the root of the notes directory (folder name ".")
  int root_fi = -1;
  for(int i = 0; i < (int)folders_.size(); ++i)
  {
    if(folders_[(size_t)i].name == ".")
    {
      root_fi = i;
      break;
    }
  }
  if(root_fi < 0)
  {
    folders_.push_back(FolderMeta{".", {}});
    root_fi = (int)folders_.size() - 1;
  }

  FolderMeta &f = folders_[(size_t)root_fi];
  NoteMeta n;
  n.id = generate_uuid();
  n.title = "demo";
  n.path = make_note_path(f.name, n.title);
  write_text_file(n.path, kDemoNoteContent);
  f.notes.insert(f.notes.begin(), std::move(n));
  active_folder_idx_ = root_fi;
  active_note_idx_ = 0;
  editing_mode_ = false;
  request_exit_edit_mode_ = false;
  load_note_content_for_active();
  save_index();
}

void App::normalize_active_indices()
{
  if(folders_.empty())
  {
    active_folder_idx_ = -1;
    active_note_idx_ = -1;
    return;
  }
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

bool App::sync_project_files()
{
  namespace fs = std::filesystem;

  std::string active_note_id;
  if(has_active_note())
    active_note_id = folders_[static_cast<std::size_t>(active_folder_idx_)]
                         .notes[static_cast<std::size_t>(active_note_idx_)]
                         .id;

  static const auto kImageExts = []() {
    std::unordered_set<std::string> s;
    for(const char *e : {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico", ".svg", ".tga", ".tiff"})
      s.insert(e);
    return s;
  }();

  auto norm_path = [](const fs::path &p) { return p.lexically_normal().string(); };

  auto lower_ext = [](const fs::path &p) {
    std::string e = p.extension().string();
    for(auto &c : e) c = (char)std::tolower((unsigned char)c);
    return e;
  };

  bool changed = false;
  std::error_code ec;

  // Missing files remain in metadata. Git/cloud batches and permission errors
  // must never be interpreted as confirmed user deletion because doing so would
  // discard stable UUID/layout information. A later verified rename may update
  // the path; explicit Notepp deletion removes metadata through its own flow.
  std::unordered_map<std::string, int> missing_fingerprint_counts;
  if(index_paths_portable_)
  {
    for(const auto &folder : folders_)
    {
      for(const auto &note : folder.notes)
      {
        if(note.path.empty()) continue;
        std::error_code exists_error;
        const bool exists = fs::exists(note.path, exists_error);
        if(exists_error)
        {
          LOG_WARNING("Cannot inspect note path '", note.path, "': ", exists_error.message());
          continue;
        }
        if(!exists && !note.content_fingerprint.empty())
          ++missing_fingerprint_counts[note.content_fingerprint];
      }
      for(const auto &image : folder.images)
      {
        std::error_code exists_error;
        (void)fs::exists(image, exists_error);
        if(exists_error)
        {
          LOG_WARNING("Cannot inspect image path '", image, "': ", exists_error.message());
          (void)0;
        }
      }
    }
  }

  // ---- 3. Build sets of already-tracked paths ----
  std::unordered_set<std::string> tracked_notes;
  std::unordered_set<std::string> tracked_images;
  for(const auto &f : folders_)
  {
    for(const auto &n : f.notes)
      if(!n.path.empty()) tracked_notes.insert(norm_path(fs::path(n.path)));
    for(const auto &img : f.images)
      if(!img.empty()) tracked_images.insert(norm_path(fs::path(img)));
  }

  // Helper: find or create folder by name, return index.
  auto find_or_create_folder = [&](const std::string &folder_name) -> int {
    for(int i = 0; i < (int)folders_.size(); ++i)
      if(folders_[(size_t)i].name == folder_name) return i;
    FolderMeta nf;
    nf.name = folder_name;
    folders_.push_back(std::move(nf));
    return (int)folders_.size() - 1;
  };

  // Helper: compute folder name and return false if the file should be skipped.
  auto folder_name_for = [&](const fs::path &p, std::string &out_folder) -> bool {
    const fs::path rel = fs::relative(p, config_.dataPath, ec);
    if(ec || rel.empty())
    {
      ec.clear();
      return false;
    }
    const fs::path parent_rel = rel.parent_path();
    if(parent_rel.empty() || parent_rel == fs::path("."))
      out_folder = ".";
    else
      out_folder = parent_rel.generic_string();
    return true;
  };

  // Count untracked content before matching. A rename is accepted only when
  // both the missing metadata and the disk candidate are unique in the folder.
  std::unordered_map<std::string, int> candidate_fingerprint_counts;
  std::vector<fs::directory_entry> disk_entries;
  {
    std::error_code count_error;
    for(const auto &entry : fs::recursive_directory_iterator(config_.dataPath, count_error))
    {
      if(count_error) break;
      if(!entry.is_regular_file(count_error))
      {
        if(count_error) break;
        continue;
      }
      disk_entries.push_back(entry);
      const fs::path &path = entry.path();
      const std::string filename = path.filename().string();
      if(lower_ext(path) != ".md" || tracked_notes.contains(norm_path(path)) ||
         filename.find(".~notepp-conflict-") != std::string::npos ||
         filename.find("notepp-local-conflict") != std::string::npos)
        continue;
      std::string folder_name;
      if(!folder_name_for(path, folder_name)) continue;
      const auto candidate = g_project_files.ensure_loaded(path);
      record_project_read(path, candidate);
      if(!candidate || !candidate.snapshot.existed) continue;
      const std::string fingerprint = notepp::note_index::content_fingerprint(
          candidate.snapshot.content);
      ++candidate_fingerprint_counts[fingerprint];
    }
    if(count_error)
    {
      LOG_WARNING("Cannot fully inspect project notes for external renames: ",
                  count_error.message());
      (void)0;
    }
  }

  // ---- 4. Reconcile the single filesystem scan ----
  for(const auto &entry : disk_entries)
  {
    const fs::path &p = entry.path();
    const std::string ext = lower_ext(p);
    const std::string norm = norm_path(p);

    const std::string filename = p.filename().string();
    if(filename.find(".~notepp-conflict-") != std::string::npos ||
       filename.find("notepp-local-conflict") != std::string::npos)
      continue;

    if(ext == ".md")
    {
      if(tracked_notes.count(norm)) continue;
      std::string folder_name;
      if(!folder_name_for(p, folder_name)) continue;
      const std::string title = p.stem().string().empty() ? "Note" : p.stem().string();
      const auto candidate = g_project_files.ensure_loaded(p);
      record_project_read(p, candidate);
      if(!candidate || !candidate.snapshot.existed) continue;
      const std::string fingerprint = notepp::note_index::content_fingerprint(
          candidate.snapshot.content);
      const std::string fingerprint_key = fingerprint;
      bool reconciled_rename = false;
      if(notepp::note_index::unique_rename_evidence(
             missing_fingerprint_counts[fingerprint_key],
             candidate_fingerprint_counts[fingerprint_key]))
      {
        int source_folder = -1;
        int source_note = -1;
        for(int folder_index = 0; folder_index < static_cast<int>(folders_.size()); ++folder_index)
        {
          for(int note_index = 0;
              note_index < static_cast<int>(folders_[static_cast<std::size_t>(folder_index)].notes.size());
              ++note_index)
          {
            const NoteMeta &note = folders_[static_cast<std::size_t>(folder_index)]
                                       .notes[static_cast<std::size_t>(note_index)];
            if(note.content_fingerprint != fingerprint || note.path.empty()) continue;
            const atomic_file::Snapshot *old_snapshot =
                g_project_files.known_snapshot(note.path);
            bool exact_content_evidence =
                notepp::note_index::strong_content_fingerprint(fingerprint);
            if(old_snapshot != nullptr && old_snapshot->existed)
              exact_content_evidence = old_snapshot->content == candidate.snapshot.content;
            if(!exact_content_evidence) continue;
            std::error_code old_exists_error;
            const bool old_exists = fs::exists(note.path, old_exists_error);
            if(old_exists_error || old_exists) continue;
            source_folder = folder_index;
            source_note = note_index;
            break;
          }
          if(source_note >= 0) break;
        }
        if(source_note >= 0)
        {
          FolderMeta &old_folder = folders_[static_cast<std::size_t>(source_folder)];
          NoteMeta moved_note = std::move(old_folder.notes[static_cast<std::size_t>(source_note)]);
          old_folder.notes.erase(old_folder.notes.begin() + source_note);
          const std::filesystem::path old_path(moved_note.path);
          moved_note.path = norm;
          moved_note.title = title;
          moved_note.unresolved_stored_path.clear();
          const int target_folder = find_or_create_folder(folder_name);
          folders_[static_cast<std::size_t>(target_folder)].notes.push_back(std::move(moved_note));
          if(g_project_files.known_snapshot(old_path) != nullptr)
          {
            g_project_files.moved(old_path, p);
            g_project_persistence_guard.moved(old_path, p);
          }
          else
          {
            g_project_files.forget(old_path);
            g_project_persistence_guard.forget(old_path);
          }
          MarkdownSupport::notify_document_moved(old_path, p);
          reconciled_rename = true;
          changed = true;
        }
      }
      if(!reconciled_rename)
      {
        const int fi = find_or_create_folder(folder_name);
        NoteMeta n;
        n.id = generate_uuid();
        n.title = title;
        n.path = norm;
        n.content_fingerprint = fingerprint;
        folders_[(size_t)fi].notes.push_back(std::move(n));
        changed = true;
      }
      tracked_notes.insert(norm);
    }
    else if(kImageExts.count(ext))
    {
      if(tracked_images.count(norm)) continue;
      std::string folder_name;
      if(!folder_name_for(p, folder_name)) continue;
      const int fi = find_or_create_folder(folder_name);
      folders_[(size_t)fi].images.push_back(norm);
      tracked_images.insert(norm);
      // Invalidate the per-folder image display cache.
      invalidate_folder_image_cache(folder_name);
      changed = true;
    }
  }

  // Detect external edits to notes by timestamp. Text is loaded only for changed files.
  // Avoid clobbering in-app edits that are queued to be saved.
  if(!state_dirty_)
  {
    for(auto &f : folders_)
    {
      for(auto &n : f.notes)
      {
        if(n.path.empty()) continue;
        std::error_code write_ec;
        const auto write_time = fs::last_write_time(n.path, write_ec);
        if(write_ec)
          continue;
        auto cached = note_content_cache_.write_time(n.path);
        if(!cached.first)
          continue;
        if(cached.second != write_time)
        {
          note_content_cache_.invalidate(n.path);
          const auto refreshed = g_project_files.load(n.path);
          record_project_read(n.path, refreshed, true);
          if(!refreshed)
          {
            LOG_ERROR(refreshed.message);
          }
          changed = true;
          if(refreshed && refreshed.snapshot.existed)
            n.content_fingerprint = notepp::note_index::content_fingerprint(
                refreshed.snapshot.content);
          if(n.path == state_file_path_ && refreshed)
          {
            markdown_text_ = refreshed.snapshot.content;
            update_note_cache(n.path, markdown_text_);
          }
        }
      }
    }
  }

  if(changed)
  {
    if(!active_note_id.empty())
    {
      for(int folder_index = 0; folder_index < static_cast<int>(folders_.size()); ++folder_index)
      {
        const auto &notes = folders_[static_cast<std::size_t>(folder_index)].notes;
        const auto found = std::find_if(notes.begin(), notes.end(), [&](const NoteMeta &note) {
          return note.id == active_note_id;
        });
        if(found == notes.end()) continue;
        active_folder_idx_ = folder_index;
        active_note_idx_ = static_cast<int>(std::distance(notes.begin(), found));
        break;
      }
    }
    save_index();
    dirty_ = true;
  }
  return changed;
}

const std::string &App::cached_note_text(const std::string &path)
{
  const auto snapshot = g_project_files.ensure_loaded(path);
  record_project_read(path, snapshot);
  if(!snapshot)
  {
    LOG_ERROR(snapshot.message);
  }
  else if(snapshot.snapshot.existed && !atomic_file::shared_writes_suspended())
  {
    int folder_index = -1;
    int note_index = -1;
    if(find_note_by_path(path, folder_index, note_index))
    {
      NoteMeta &note = folders_[static_cast<std::size_t>(folder_index)]
                           .notes[static_cast<std::size_t>(note_index)];
      if(!notepp::note_index::strong_content_fingerprint(note.content_fingerprint))
      {
        note.content_fingerprint = notepp::note_index::content_fingerprint(
            snapshot.snapshot.content);
        layout_dirty_ = true;
      }
    }
  }
#ifdef NOTEPP_DEBUG_UI
  const auto before = note_content_cache_.disk_read_count();
  const auto &text = note_content_cache_.get(path);
  if(note_content_cache_.disk_read_count() != before)
    ++g_dbg_disk_reads;
  return text;
#else
  return note_content_cache_.get(path);
#endif
}

void App::update_note_cache(const std::string &path, std::string text)
{
  note_content_cache_.update(path, std::move(text));
}

void App::invalidate_note_cache(const std::string &path)
{
  note_content_cache_.invalidate(path);
}

void App::load_note_content_for_active()
{
  if(!history_replay_in_progress_) flush_pending_text_history();

  const std::string previous_note_path = state_file_path_;
  const auto clear_search_match_state_if_note_changed = [&]() {
    if(state_file_path_ == previous_note_path) return;
    search_editor_scroll_note_path_.clear();
    search_editor_scroll_pos_ = -1;
    search_editor_selection_note_path_.clear();
    search_editor_selection_start_ = -1;
    search_editor_selection_end_ = -1;
    search_editor_selection_focus_pending_ = false;
    search_editor_match_note_path_.clear();
    search_editor_match_offset_ = -1;
    search_editor_match_length_ = 0;
    if(preview_edit_cursor_note_path_ != state_file_path_)
    {
      preview_edit_cursor_note_path_.clear();
      preview_edit_cursor_pos_ = -1;
      request_preview_heading({}, {}, 0);
      request_preview_source_offset({}, 0);
    }
  };

  ensure_default_index();
  if(!has_active_note())
  {
    state_file_path_.clear();
    note_title_ = "Note";
    markdown_text_.clear();
    discard_pending_text_history();
    request_undo_edit_ = false;
    request_redo_edit_ = false;
    clear_search_match_state_if_note_changed();
    return;
  }

  NoteMeta &n = folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_];
  state_file_path_ = n.path;
  note_title_ = n.title;
  if(!n.unresolved_stored_path.empty())
  {
    state_file_path_.clear();
    markdown_text_.clear();
    discard_pending_text_history();
    request_undo_edit_ = false;
    request_redo_edit_ = false;
    clear_search_match_state_if_note_changed();
    return;
  }

  const auto loaded_note = g_project_files.load(state_file_path_);
  record_project_read(state_file_path_, loaded_note, true);
  if(!loaded_note)
  {
    LOG_ERROR(loaded_note.message);
    markdown_text_.clear();
    // Do not immediately persist the empty placeholder. If the user edits it,
    // normal editor change tracking marks it dirty and save_project_text()
    // preserves those bytes in a recovery note without touching unseen data.
    state_dirty_ = false;
  }
  else if(loaded_note.snapshot.existed)
  {
    markdown_text_ = loaded_note.snapshot.content;
    const std::string fingerprint = notepp::note_index::content_fingerprint(markdown_text_);
    if(n.content_fingerprint != fingerprint)
    {
      n.content_fingerprint = fingerprint;
      layout_dirty_ = true;
    }
    update_note_cache(state_file_path_, markdown_text_);
  }
  else
  {
    markdown_text_.clear();
    update_note_cache(state_file_path_, markdown_text_);
    state_dirty_ = true;
  }
  discard_pending_text_history();
  request_undo_edit_ = false;
  request_redo_edit_ = false;
  clear_search_match_state_if_note_changed();
}

void App::set_active_note(int folder_idx, int note_idx)
{
  flush_pending_text_history();
  ensure_default_index();
  folder_idx = std::max(0, std::min(folder_idx, (int)folders_.size() - 1));
  const int note_count = (int)folders_[(size_t)folder_idx].notes.size();
  if(note_count <= 0)
    note_idx = -1;
  else
    note_idx = std::max(0, std::min(note_idx, note_count - 1));

  state_dirty_ = true;
  const int prev_folder = active_folder_idx_;
  active_folder_idx_ = folder_idx;
  active_note_idx_ = note_idx;
  folder_overview_mode_ = false;
  if(folder_idx != prev_folder)
    apply_folder_settings(folder_idx);
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
  const std::string folder_name = folders_.empty() ? "." : folders_[(size_t)active_folder_idx_].name;
  std::filesystem::path new_path = make_note_path(folder_name, safe_title);

  if(new_path.string() == state_file_path_)
  {
    note_title_ = safe_title;
    sync_active_note_meta();
    return;
  }

  std::error_code exists_error;
  std::filesystem::path current_path(state_file_path_);
  if(std::filesystem::exists(current_path, exists_error) &&
     !move_project_file(current_path, new_path))
    return;
  if(exists_error)
  {
    record_project_save_error(current_path, exists_error.message());
    return;
  }

  state_file_path_ = new_path.string();
  note_title_ = safe_title;
  sync_active_note_meta();
  state_dirty_ = true;
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
    std::error_code exists_error;
    std::filesystem::path old_path(n.path);
    if(std::filesystem::exists(old_path, exists_error) &&
       !move_project_file(old_path, new_path))
      return;
    if(exists_error)
    {
      record_project_save_error(old_path, exists_error.message());
      return;
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

void App::push_undo_snapshot_from(const std::string &snapshot)
{
  if(history_replay_in_progress_ || state_file_path_.empty()) return;
  deferred_text_snapshot_before_ = snapshot;
}

void App::push_undo_snapshot()
{
  push_undo_snapshot_from(markdown_text_);
}

void App::apply_undo_snapshot()
{
  (void)apply_global_undo();
}

void App::apply_redo_snapshot()
{
  (void)apply_global_redo();
}

std::string App::capture_workspace_snapshot() const
{
  Json root;
  root["active_folder"] = active_folder_idx_;
  root["active_note"] = active_note_idx_;
  root["folder_overview"] = folder_overview_mode_;
  root["editing_mode"] = editing_mode_;
  root["layout_locked"] = layout_locked_;
  root["detached_note_windows"] = detached_note_windows_enabled_;
  root["dockers_enabled"] = dockers_enabled_;
  root["terminal_height"] = terminal_height_;
  root["preview_state"] = capture_preview_state_snapshot();

  std::vector<std::string> pending_paths = pending_fs_delete_paths_;
  std::sort(pending_paths.begin(), pending_paths.end());
  root["pending_delete_paths"] = pending_paths;

  Json folders_json = Json::array();
  for(const FolderMeta &folder : folders_)
  {
    Json folder_json;
    folder_json["name"] = folder.name;
    const bool is_active = (active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size() &&
                            &folder == &folders_[(size_t)active_folder_idx_]);
    folder_json["layout_locked"] = is_active ? layout_locked_ : folder.layout_locked;
    folder_json["detached_note_windows"] = is_active ? detached_note_windows_enabled_ : folder.detached_note_windows;
    folder_json["dockers_enabled"] = is_active ? dockers_enabled_ : folder.dockers_enabled;
    folder_json["drawings_visible"] = is_active ? drawings_visible_ : folder.drawings_visible;
    folder_json["grid_visible"] = is_active ? grid_visible_ : folder.grid_visible;

    Json notes_json = Json::array();
    for(const NoteMeta &note : folder.notes)
    {
      Json note_json;
      note_json["id"] = note.id;
      note_json["title"] = note.title;
      note_json["path"] = note.path;
      note_json["use_custom_color"] = note.use_custom_color;
      note_json["color_r"] = note.color_r;
      note_json["color_g"] = note.color_g;
      note_json["color_b"] = note.color_b;
      note_json["pos_x"] = note.pos_x;
      note_json["pos_y"] = note.pos_y;
      note_json["width"] = note.width;
      note_json["height"] = note.height;
      note_json["has_layout"] = note.has_layout;
      note_json["hidden"] = note.hidden;
      note_json["always_on_top"] = note.always_on_top;
      note_json["dock_id"] = note.dock_id;
      note_json["content"] = (note.path == state_file_path_) ? markdown_text_ : read_text_file(note.path);
      notes_json.push_back(std::move(note_json));
    }

    folder_json["notes"] = std::move(notes_json);
    folders_json.push_back(std::move(folder_json));
  }
  root["folders"] = std::move(folders_json);

  std::vector<std::string> drawing_keys;
  drawing_keys.reserve(g_folder_drawings.size());
  for(const auto &[folder_key, strokes] : g_folder_drawings)
  {
    (void)strokes;
    drawing_keys.push_back(folder_key);
  }
  std::sort(drawing_keys.begin(), drawing_keys.end());

  Json drawings_json = Json::array();
  for(const std::string &folder_key : drawing_keys)
  {
    Json folder_json;
    folder_json["folder"] = folder_key;

    Json strokes_json = Json::array();
    const auto it = g_folder_drawings.find(folder_key);
    if(it != g_folder_drawings.end())
    {
      for(const FreeStroke &stroke : it->second)
      {
        Json stroke_json;
        stroke_json["thickness"] = stroke.thickness;
        stroke_json["color"] = Json::array({stroke.color.x, stroke.color.y, stroke.color.z, stroke.color.w});

        Json points_json = Json::array();
        for(const ImVec2 &point : stroke.points)
        {
          points_json.push_back(Json::array({point.x, point.y}));
        }
        stroke_json["points"] = std::move(points_json);
        strokes_json.push_back(std::move(stroke_json));
      }
    }

    folder_json["strokes"] = std::move(strokes_json);
    drawings_json.push_back(std::move(folder_json));
  }
  root["drawings"] = std::move(drawings_json);

  // Profile state (includes soft-deleted so undo can recover them)
  Json profiles_json = Json::array();
  for(const auto &p : layout_profiles_)
  {
    Json pj;
    pj["id"] = p.id;
    pj["name"] = p.name;
    pj["window_maximized"] = p.window_maximized;
    pj["window_x"] = p.window_x;
    pj["window_y"] = p.window_y;
    pj["window_w"] = p.window_w;
    pj["window_h"] = p.window_h;
    pj["pending_delete"] = p.pending_delete;
    profiles_json.push_back(std::move(pj));
  }
  root["layout_profiles"] = std::move(profiles_json);
  root["active_profile_id"] = active_profile_id_;
  root["maximized_profile_id"] = maximized_profile_id_;
  root["reduced_profile_id"] = reduced_profile_id_;

  return root.dump();
}

void App::apply_workspace_snapshot(std::string_view snapshot)
{
  const Json root = Json::parse(snapshot.begin(), snapshot.end(), nullptr, false);
  if(root.is_discarded()) return;

  discard_pending_text_history();
  const bool previous_replay = history_replay_in_progress_;
  history_replay_in_progress_ = true;

  std::unordered_set<std::string> current_paths;
  for(const FolderMeta &folder : folders_)
  {
    for(const NoteMeta &note : folder.notes)
    {
      if(!note.path.empty()) current_paths.insert(note.path);
    }
  }
  for(const std::string &path : pending_fs_delete_paths_)
  {
    if(!path.empty()) current_paths.insert(path);
  }

  std::vector<FolderMeta> restored_folders;
  // Read root-level settings as defaults for backward compat with old snapshots
  const int root_active_folder = root.value("active_folder", 0);
  const bool root_layout_locked = root.value("layout_locked", false);
  const bool root_detached = root.value("detached_note_windows", false);
  const bool root_dockers = root.value("dockers_enabled", false);
  terminal_height_ = std::clamp(root.value("terminal_height", terminal_height_), 180.0F, 2000.0F);

  std::unordered_map<std::string, std::string> restored_contents;
  int folder_parse_idx = 0;
  if(root.contains("folders") && root["folders"].is_array())
  {
    for(const Json &folder_json : root["folders"])
    {
      const bool is_active = (folder_parse_idx == root_active_folder);
      FolderMeta folder;
      folder.name = folder_json.value("name", std::string("General"));
      folder.layout_locked = folder_json.value("layout_locked", is_active ? root_layout_locked : false);
      folder.detached_note_windows = folder_json.value("detached_note_windows", is_active ? root_detached : false);
      folder.dockers_enabled = folder_json.value("dockers_enabled", is_active ? root_dockers : false);
      folder.drawings_visible = folder_json.value("drawings_visible", true);
      folder.grid_visible = folder_json.value("grid_visible", false);

      if(folder_json.contains("notes") && folder_json["notes"].is_array())
      {
        for(const Json &note_json : folder_json["notes"])
        {
          NoteMeta note;
          note.id = note_json.value("id", std::string{});
          if(note.id.empty()) note.id = generate_uuid();
          note.title = note_json.value("title", std::string("Note"));
          note.path = note_json.value("path", std::string());
          note.use_custom_color = note_json.value("use_custom_color", false);
          note.color_r = note_json.value("color_r", 0.0f);
          note.color_g = note_json.value("color_g", 0.0f);
          note.color_b = note_json.value("color_b", 0.0f);
          note.pos_x = note_json.value("pos_x", 0.0f);
          note.pos_y = note_json.value("pos_y", 0.0f);
          note.width = note_json.value("width", 520.0f);
          note.height = note_json.value("height", 260.0f);
          note.has_layout = note_json.value("has_layout", false);
          note.hidden = note_json.value("hidden", false);
          note.always_on_top = note_json.value("always_on_top", false);
          note.dock_id = note_json.value("dock_id", 0u);
          if(note.path.empty()) note.path = make_note_path(folder.name, note.title);

          restored_contents[note.path] = note_json.value("content", std::string());
          folder.notes.push_back(std::move(note));
        }
      }

      restored_folders.push_back(std::move(folder));
      ++folder_parse_idx;
    }
  }

  std::unordered_map<std::string, std::vector<FreeStroke>> restored_drawings;
  if(root.contains("drawings") && root["drawings"].is_array())
  {
    for(const Json &folder_json : root["drawings"])
    {
      const std::string folder_key = folder_json.value("folder", std::string());
      if(folder_key.empty()) continue;

      std::vector<FreeStroke> strokes;
      if(folder_json.contains("strokes") && folder_json["strokes"].is_array())
      {
        for(const Json &stroke_json : folder_json["strokes"])
        {
          FreeStroke stroke;
          stroke.thickness = stroke_json.value("thickness", 2.2f);

          if(stroke_json.contains("color") && stroke_json["color"].is_array() && stroke_json["color"].size() == 4)
          {
            stroke.color = ImVec4(
                stroke_json["color"][0].get<float>(),
                stroke_json["color"][1].get<float>(),
                stroke_json["color"][2].get<float>(),
                stroke_json["color"][3].get<float>());
          }

          if(stroke_json.contains("points") && stroke_json["points"].is_array())
          {
            for(const Json &point_json : stroke_json["points"])
            {
              if(!point_json.is_array() || point_json.size() != 2) continue;
              stroke.points.push_back(ImVec2(point_json[0].get<float>(), point_json[1].get<float>()));
            }
          }

          strokes.push_back(std::move(stroke));
        }
      }

      restored_drawings[folder_key] = std::move(strokes);
    }
  }

  std::unordered_set<std::string> target_paths;
  target_paths.reserve(restored_contents.size());
  for(const auto &[path, content] : restored_contents)
  {
    (void)content;
    target_paths.insert(path);
  }

  for(const auto &[path, content] : restored_contents)
  {
    if(write_text_file(path, content))
    {
      std::error_code backup_error;
      std::filesystem::remove(std::filesystem::path(path + ".bak"), backup_error);
      if(!backup_error) g_project_files.forget(std::filesystem::path(path + ".bak"));
    }
  }
  for(const std::string &path : current_paths)
  {
    if(path.empty() || target_paths.count(path) != 0) continue;
    if(soft_delete_project_file(path) &&
       std::find(pending_fs_delete_paths_.begin(), pending_fs_delete_paths_.end(), path) ==
           pending_fs_delete_paths_.end())
      pending_fs_delete_paths_.push_back(path);
  }

  folders_ = std::move(restored_folders);
  g_folder_drawings = std::move(restored_drawings);
  g_draw_undo.clear();
  g_draw_redo.clear();
  g_drawings_legacy_checked.clear();

  pending_fs_delete_paths_.clear();
  if(root.contains("pending_delete_paths") && root["pending_delete_paths"].is_array())
  {
    for(const Json &path_json : root["pending_delete_paths"])
    {
      pending_fs_delete_paths_.push_back(path_json.get<std::string>());
    }
  }

  apply_preview_state_snapshot(root.value("preview_state", std::string("{\"documents\":{}}")));

  active_folder_idx_ = root.value("active_folder", 0);
  active_note_idx_ = root.value("active_note", -1);
  folder_overview_mode_ = root.value("folder_overview", false);
  editing_mode_ = root.value("editing_mode", false);
  request_exit_edit_mode_ = false;

  // Restore profile list from snapshot (for undo/redo of profile operations)
  if(root.contains("layout_profiles") && root["layout_profiles"].is_array())
  {
    // Preserve per-note layouts (not in snapshot) by building a map from current profiles
    std::unordered_map<std::string, std::unordered_map<std::string, NoteLayoutData>> saved_layouts;
    for(const auto &p : layout_profiles_)
      saved_layouts[p.id] = p.note_layouts;

    layout_profiles_.clear();
    for(const Json &pj : root["layout_profiles"])
    {
      LayoutProfile p;
      p.id = pj.value("id", std::string{});
      if(p.id.empty()) continue;
      p.name = pj.value("name", std::string("Profile"));
      p.window_maximized = pj.value("window_maximized", true);
      p.window_x = pj.value("window_x", 100);
      p.window_y = pj.value("window_y", 100);
      p.window_w = pj.value("window_w", 1100);
      p.window_h = pj.value("window_h", 700);
      p.pending_delete = pj.value("pending_delete", false);
      // Restore note layouts if we have them
      auto lit = saved_layouts.find(p.id);
      if(lit != saved_layouts.end())
        p.note_layouts = lit->second;
      layout_profiles_.push_back(std::move(p));
    }
    active_profile_id_ = root.value("active_profile_id", active_profile_id_);
    maximized_profile_id_ = root.value("maximized_profile_id", maximized_profile_id_);
    reduced_profile_id_ = root.value("reduced_profile_id", reduced_profile_id_);

    // Apply the restored active profile's window config
    window_profile_check_pending_ = false;
    window_profile_check_delay_ = 0;
    const LayoutProfile *ap = find_active_profile();
    if(ap) apply_profile(*ap, true);
  }

  ensure_default_index();
  normalize_active_indices();
  apply_folder_settings(active_folder_idx_);

  if(has_active_note())
  {
    const NoteMeta &active_note = folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_];
    state_file_path_ = active_note.path;
    note_title_ = active_note.title;
    markdown_text_ = restored_contents[active_note.path];
    normalize_input_text_buffer(markdown_text_);
  }
  else
  {
    state_file_path_.clear();
    note_title_ = "Note";
    markdown_text_.clear();
  }

  g_drawings_dirty = false;
  layout_dirty_ = false;
  force_note_layout_restore_ = true;
  state_dirty_ = true;

  history_replay_in_progress_ = previous_replay;
}

std::string App::capture_text_context_snapshot() const
{
  Json root;
  root["active_folder"] = active_folder_idx_;
  root["active_note"] = active_note_idx_;
  root["folder_overview"] = folder_overview_mode_;
  root["editing_mode"] = editing_mode_;
  root["active_note_path"] = has_active_note() ? folders_[(size_t)active_folder_idx_].notes[(size_t)active_note_idx_].path : std::string();
  return root.dump();
}

void App::apply_text_history_state(std::string_view note_path, std::string_view text, std::string_view context_snapshot)
{
  const Json context = Json::parse(context_snapshot.begin(), context_snapshot.end(), nullptr, false);

  discard_pending_text_history();
  const bool previous_replay = history_replay_in_progress_;
  history_replay_in_progress_ = true;

  if(!write_text_file(std::string(note_path), text))
  {
    history_replay_in_progress_ = previous_replay;
    return;
  }

  folder_overview_mode_ = context.value("folder_overview", folder_overview_mode_);
  editing_mode_ = context.value("editing_mode", editing_mode_);

  int target_folder_idx = context.value("active_folder", active_folder_idx_);
  int target_note_idx = context.value("active_note", active_note_idx_);
  bool found_note = false;

  if(context.contains("active_note_path"))
  {
    const std::string active_note_path = context.value("active_note_path", std::string());
    found_note = find_note_by_path(active_note_path, target_folder_idx, target_note_idx);
  }
  if(!found_note)
  {
    found_note = find_note_by_path(note_path, target_folder_idx, target_note_idx);
  }

  active_folder_idx_ = target_folder_idx;
  active_note_idx_ = found_note ? target_note_idx : target_note_idx;
  ensure_default_index();
  normalize_active_indices();
  load_note_content_for_active();

  if(has_active_note() && state_file_path_ == note_path)
  {
    markdown_text_.assign(text.begin(), text.end());
    normalize_input_text_buffer(markdown_text_);
  }

  state_dirty_ = true;
  history_replay_in_progress_ = previous_replay;
}

void App::record_workspace_history_action(std::string_view label, std::string before_snapshot)
{
  if(history_replay_in_progress_) return;

  flush_pending_text_history();
  const std::string after_snapshot = capture_workspace_snapshot();
  if(before_snapshot == after_snapshot) return;

  const std::string action_label = label.empty() ? "Edit workspace" : std::string(label);
  const std::string debug_context = make_history_debug_context();
  history_.push_executed(std::make_unique<NoteHistory::LambdaCommand>(
      action_label,
      debug_context,
      [this, snapshot = after_snapshot]() { apply_workspace_snapshot(snapshot); },
      [this, snapshot = std::move(before_snapshot)]() { apply_workspace_snapshot(snapshot); }));
}

void App::apply_preview_history_state(std::string_view note_path, std::string_view text, std::string_view preview_state_snapshot)
{
  discard_pending_text_history();
  const bool previous_replay = history_replay_in_progress_;
  history_replay_in_progress_ = true;

  write_text_file(std::string(note_path), text);
  apply_preview_state_snapshot(preview_state_snapshot);

  if(!state_file_path_.empty() && state_file_path_ == note_path)
  {
    markdown_text_.assign(text.begin(), text.end());
    normalize_input_text_buffer(markdown_text_);
  }

  history_replay_in_progress_ = previous_replay;
}

void App::record_preview_history_action(std::string_view label, std::string_view note_path, const std::string &before_text, const std::string &after_text, const std::string &before_preview_state, const std::string &after_preview_state)
{
  if(history_replay_in_progress_) return;
  if(before_text == after_text && before_preview_state == after_preview_state) return;

  flush_pending_text_history();

  const std::string target_note_path(note_path);
  const std::string action_label = label.empty() ? "Edit preview widget" : std::string(label);
  const std::string debug_context = make_history_debug_context(target_note_path);

  history_.push_executed(std::make_unique<NoteHistory::LambdaCommand>(
      action_label,
      debug_context,
      [this, target_note_path, after_text, after_preview_state]() {
        apply_preview_history_state(target_note_path, after_text, after_preview_state);
      },
      [this, target_note_path, before_text, before_preview_state]() {
        apply_preview_history_state(target_note_path, before_text, before_preview_state);
      }));
}

void App::record_text_history_action(std::string_view label, const std::string &before_text, const std::string &after_text)
{
  if(history_replay_in_progress_ || state_file_path_.empty() || before_text == after_text) return;

  flush_pending_text_history();

  const std::string note_path = state_file_path_;
  const std::string context_snapshot = capture_text_context_snapshot();
  const std::string action_label = label.empty() ? "Edit text" : std::string(label);
  const std::string debug_context = make_history_debug_context(note_path);

  history_.push_executed(std::make_unique<NoteHistory::LambdaCommand>(
      action_label,
      debug_context,
      [this, note_path, after_text, context_snapshot]() { apply_text_history_state(note_path, after_text, context_snapshot); },
      [this, note_path, before_text, context_snapshot]() { apply_text_history_state(note_path, before_text, context_snapshot); }));
}

void App::update_pending_text_history(std::string_view label, const std::string &before_text, const std::string &after_text, bool start_new_chunk)
{
  if(history_replay_in_progress_ || state_file_path_.empty() || before_text == after_text) return;

  if(start_new_chunk) flush_pending_text_history();

  const std::string context_snapshot = capture_text_context_snapshot();
  if(!pending_text_history_.active || pending_text_history_.note_path != state_file_path_)
  {
    pending_text_history_.active = true;
    pending_text_history_.label = label.empty() ? "Edit text" : std::string(label);
    pending_text_history_.note_path = state_file_path_;
    pending_text_history_.before_text = before_text;
    pending_text_history_.after_text = after_text;
    pending_text_history_.context_snapshot = context_snapshot;
    return;
  }

  pending_text_history_.label = label.empty() ? pending_text_history_.label : std::string(label);
  pending_text_history_.after_text = after_text;
  pending_text_history_.context_snapshot = context_snapshot;
}

void App::flush_pending_text_history()
{
  if(history_replay_in_progress_ || !pending_text_history_.active) return;

  PendingTextHistory pending = std::move(pending_text_history_);
  pending_text_history_ = PendingTextHistory{};

  if(pending.note_path.empty() || pending.before_text == pending.after_text) return;

  const std::string action_label = pending.label.empty() ? "Edit text" : pending.label;
  const std::string debug_context = make_history_debug_context(pending.note_path);
  history_.push_executed(std::make_unique<NoteHistory::LambdaCommand>(
      action_label,
      debug_context,
      [this, note_path = pending.note_path, after_text = pending.after_text, context = pending.context_snapshot]() {
        apply_text_history_state(note_path, after_text, context);
      },
      [this, note_path = pending.note_path, before_text = pending.before_text, context = pending.context_snapshot]() {
        apply_text_history_state(note_path, before_text, context);
      }));
}

void App::discard_pending_text_history()
{
  pending_text_history_ = PendingTextHistory{};
}

bool App::apply_global_undo()
{
  flush_pending_text_history();
  const std::string label(history_.next_undo_label());
  if(!history_.undo()) return false;
  show_history_indicator("Undo", label, ImVec4(0.93f, 0.58f, 0.24f, 1.0f));
  return true;
}

bool App::apply_global_redo()
{
  flush_pending_text_history();
  const std::string label(history_.next_redo_label());
  if(!history_.redo()) return false;
  show_history_indicator("Redo", label, ImVec4(0.22f, 0.74f, 0.58f, 1.0f));
  return true;
}

bool App::find_note_by_path(std::string_view path, int &folder_idx, int &note_idx) const
{
  for(int fi = 0; fi < (int)folders_.size(); ++fi)
  {
    const FolderMeta &folder = folders_[(size_t)fi];
    for(int ni = 0; ni < (int)folder.notes.size(); ++ni)
    {
      if(folder.notes[(size_t)ni].path == path)
      {
        folder_idx = fi;
        note_idx = ni;
        return true;
      }
    }
  }
  return false;
}

std::string App::make_history_debug_context(std::string_view preferred_note_path) const
{
  int folder_idx = active_folder_idx_;
  int note_idx = active_note_idx_;
  bool note_found = false;

  if(!preferred_note_path.empty())
  {
    note_found = find_note_by_path(preferred_note_path, folder_idx, note_idx);
  }
  else if(has_active_note())
  {
    note_found = true;
  }

  if(note_found && folder_idx >= 0 && folder_idx < (int)folders_.size())
  {
    const FolderMeta &folder = folders_[(size_t)folder_idx];
    if(note_idx >= 0 && note_idx < (int)folder.notes.size())
    {
      return folder.name + " / " + folder.notes[(size_t)note_idx].title;
    }
  }

  if(folder_idx >= 0 && folder_idx < (int)folders_.size())
  {
    return std::string("Folder: ") + folders_[(size_t)folder_idx].name;
  }

  if(!preferred_note_path.empty())
  {
    return std::string(preferred_note_path);
  }

  return "Workspace";
}

void App::render_debug_history_window() const
{
#ifndef NOTEPP_DEBUG_UI
  return;
#else
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  if(viewport == nullptr) return;

  const std::vector<NoteHistory::DebugEntry> undo_entries = history_.debug_undo_entries();
  const std::vector<NoteHistory::DebugEntry> redo_entries = history_.debug_redo_entries();

  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 20.0f, viewport->WorkPos.y + 20.0f),
      ImGuiCond_FirstUseEver,
      ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.9f);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoSavedSettings;

  if(!ImGui::Begin("History Debug", nullptr, flags))
  {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Debug-only history inspector");
  ImGui::TextDisabled("Top item is the next action that Ctrl+Z / Ctrl+Y will use.");
  ImGui::Spacing();
  const ImGuiIO &io = ImGui::GetIO();
  ImGui::Text("FPS: %.1f  (%.2f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
  ImGui::Spacing();
  ImGui::SeparatorText("Frame budget (ms)");
  ImGui::Text("  begin : %6.2f ms", g_dbg_begin_ms);
  ImGui::Text("  ui    : %6.2f ms", g_dbg_ui_ms);
  ImGui::Text("  end   : %6.2f ms", g_dbg_end_ms);
  ImGui::Text("  swap  : %6.2f ms  <--", g_dbg_swap_ms);
  ImGui::Spacing();
  const double now = ImGui::GetTime();
  if(now >= g_dbg_next_metrics_time)
  {
    g_dbg_disk_reads_last_sec = g_dbg_disk_reads;
    g_dbg_disk_reads = 0;
    g_dbg_next_metrics_time = now + 1.0;
  }
  ImGui::SeparatorText("Performance counters");
  ImGui::Text("disk reads/sec: %u", g_dbg_disk_reads_last_sec);
  ImGui::Text("visible notes: %d", folder_overview_mode_ && active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size()
                                       ? (int)std::count_if(folders_[(size_t)active_folder_idx_].notes.begin(), folders_[(size_t)active_folder_idx_].notes.end(), [](const NoteMeta &n) { return !n.hidden; })
                                       : (has_active_note() ? 1 : 0));
  ImGui::Text("note text cache entries: %d", (int)note_content_cache_.size());
  ImGui::Text("OpenGL renderer: %s", (const char *)glGetString(GL_RENDERER));
  ImGui::Spacing();
  ImGui::Text("Undo: %d", (int)undo_entries.size());
  ImGui::SameLine();
  ImGui::Text("Redo: %d", (int)redo_entries.size());

  auto render_entries = [](const char *title, const std::vector<NoteHistory::DebugEntry> &entries, ImVec4 accent) {
    ImGui::SeparatorText(title);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.45f));
    if(ImGui::BeginChild(title, ImVec2(0.0f, 145.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
      if(entries.empty())
      {
        ImGui::TextDisabled("Empty");
      }
      else
      {
        for(size_t i = 0; i < entries.size(); ++i)
        {
          const NoteHistory::DebugEntry &entry = entries[i];
          if(i == 0)
          {
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::Text("#%d  %s", (int)(i + 1), entry.label.c_str());
            ImGui::PopStyleColor();
          }
          else
          {
            ImGui::Text("#%d  %s", (int)(i + 1), entry.label.c_str());
          }

          if(entry.context.empty())
            ImGui::TextDisabled("Context: Workspace");
          else
            ImGui::TextDisabled("Context: %s", entry.context.c_str());

          if(i + 1 < entries.size()) ImGui::Separator();
        }
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
  };

  render_entries("Undo Stack", undo_entries, ImVec4(0.93f, 0.58f, 0.24f, 1.0f));
  render_entries("Redo Stack", redo_entries, ImVec4(0.22f, 0.74f, 0.58f, 1.0f));

  if(ImGui::BeginPopupContextWindow("##debug_ctx", ImGuiPopupFlags_MouseButtonRight))
  {
    if(ImGui::MenuItem("Copy debug info"))
    {
      char buf[512];
      std::snprintf(buf, sizeof(buf),
                    "FPS: %.1f  (%.2f ms/frame)\n"
                    "begin : %6.2f ms\n"
                    "ui    : %6.2f ms\n"
                    "end   : %6.2f ms\n"
                    "swap  : %6.2f ms\n"
                    "Undo: %d  Redo: %d",
                    io.Framerate, 1000.0f / io.Framerate,
                    g_dbg_begin_ms, g_dbg_ui_ms, g_dbg_end_ms, g_dbg_swap_ms,
                    (int)undo_entries.size(), (int)redo_entries.size());
      ImGui::SetClipboardText(buf);
    }
    ImGui::EndPopup();
  }

  ImGui::End();
#endif
}

bool App::frame_begin()
{
  if(command_api_ != nullptr)
  {
    command_ipc_server_.poll([this](std::string_view request) {
      try
      {
        const auto parsed = nlohmann::json::parse(request);
        const auto executed = command_api_->execute(parsed);
        if(executed.ok) post_command_mutation(parsed, executed);
        nlohmann::json response = nlohmann::json::parse(executed.serialize());
        if(parsed.contains("id")) response["id"] = parsed.at("id");
        return response.dump();
      }
      catch(const std::exception &error)
      {
        nlohmann::json malformed{{"success", false}, {"ok", false},
                                 {"error", {{"code", "invalid_request"},
                                             {"message", error.what()}}}};
        try
        {
          const auto parsed = nlohmann::json::parse(request);
          if(parsed.is_object() && parsed.contains("id")) malformed["id"] = parsed.at("id");
        }
        catch(const std::exception &) { }
        return malformed.dump();
      }
    });
  }
  if(git_sync_in_progress_ && git_sync_future_.valid())
  {
    try
    {
      if(git_sync_future_.wait_for(std::chrono::seconds(0)) ==
         std::future_status::ready)
        finish_git_operation();
    }
    catch(const std::exception &error)
    {
      git_sync_in_progress_ = false;
      git_operation_kind_ = GitOperationKind::none;
      atomic_file::set_shared_writes_suspended(false);
      record_git_status(notepp::git_sync::exception_result(
                            "Polling Git Sync", error.what())
                            .status);
      if(close_requested_) advance_close();
    }
    catch(...)
    {
      git_sync_in_progress_ = false;
      git_operation_kind_ = GitOperationKind::none;
      atomic_file::set_shared_writes_suspended(false);
      record_git_status(notepp::git_sync::exception_result("Polling Git Sync").status);
      if(close_requested_) advance_close();
    }
  }
  {
    const ImGuiIO &_io = ImGui::GetIO();
    if((_io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0)
      set_dockers_enabled(true);
    if(bool(_io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != detached_note_windows_enabled_)
      set_detached_note_windows_enabled(detached_note_windows_enabled_);
  }
  pinned_topmost_viewports_.clear();

  // frame_begin runs before NewFrame applies mouse clicks; track the clicked window
  // so a click away from either non-modal dialog immediately releases its shortcuts.
  // Terminal focus is also resolved during frame_ui(), so use the click target for
  // key events delivered in the same SDL batch instead of the previous frame's
  // terminal_.hasFocus() value.
  bool search_dialog_mouse_focus_override_valid = false;
  bool search_dialog_mouse_focus_override = false;
  bool note_switcher_mouse_focus_override_valid = false;
  bool note_switcher_mouse_focus_override = false;
  bool terminal_mouse_focus_override_valid = false;
  bool terminal_mouse_focus_override = false;
  const auto search_dialog_contains_window = [](ImGuiWindow *window) {
    if(window == nullptr || ImGui::GetCurrentContext() == nullptr) return false;
    ImGuiWindow *search_dialog = ImGui::FindWindowByName(Lang::t("Search"));
    return search_dialog != nullptr && ImGui::IsWindowWithinBeginStackOf(window, search_dialog);
  };
  const auto search_dialog_has_keyboard_focus = [&]() {
    if(search_dialog_mouse_focus_override_valid) return search_dialog_mouse_focus_override;
    return GImGui != nullptr && search_dialog_contains_window(GImGui->NavWindow);
  };
  const auto note_switcher_contains_window = [](ImGuiWindow *window) {
    if(window == nullptr || ImGui::GetCurrentContext() == nullptr) return false;
    ImGuiWindow *switcher = ImGui::FindWindowByName("Quick Note Switcher");
    return switcher != nullptr && ImGui::IsWindowWithinBeginStackOf(window, switcher);
  };
  const auto note_switcher_has_keyboard_focus = [&]() {
    if(note_switcher_mouse_focus_override_valid) return note_switcher_mouse_focus_override;
    return GImGui != nullptr && note_switcher_contains_window(GImGui->NavWindow);
  };
  const auto terminal_contains_window = [](ImGuiWindow *window) {
    if(window == nullptr || ImGui::GetCurrentContext() == nullptr) return false;
    ImGuiWindow *terminal_window = ImGui::FindWindowByName("Terminal##notepp");
    return terminal_window != nullptr && ImGui::IsWindowWithinBeginStackOf(window, terminal_window);
  };
  const auto terminal_has_keyboard_focus = [&]() {
    if(terminal_mouse_focus_override_valid) return terminal_mouse_focus_override;
    // The terminal is a separately rendered input surface. When it is
    // visible, its focus flag may still reflect the previous frame (notably
    // immediately after opening it from Command Finder); treat it as the
    // keyboard owner until an explicit click selects another window.
    return terminal_.hasFocus() || terminal_visible_;
  };
  const auto is_terminal_toggle_key = [](SDL_Keycode key) {
    return key == SDLK_BACKQUOTE || key == static_cast<SDL_Keycode>(0xF1) ||
           key == static_cast<SDL_Keycode>(0xD1);
  };

  bool had_event = false;
  SDL_Event event;
  while(SDL_PollEvent(&event))
  {
    if(event.type == kGitSyncCompletedEvent)
    {
      finish_git_operation();
      had_event = true;
      continue;
    }
    if(event.type == kProjectFilesChangedEvent)
    {
      if(!git_sync_in_progress_ && sync_project_files()) had_event = true;
      continue;
    }

    had_event = true;

    if(event.type == SDL_KEYDOWN)
    {
      const Uint16 terminal_mod = event.key.keysym.mod;
      const bool terminal_ctrl = (terminal_mod & KMOD_CTRL) != 0;
      const bool terminal_shift = (terminal_mod & KMOD_SHIFT) != 0;
      const bool terminal_alt = (terminal_mod & KMOD_ALT) != 0;
      if(terminal_ctrl && !terminal_alt && is_terminal_toggle_key(event.key.keysym.sym))
      {
        if(terminal_shift)
        {
          if(terminal_visible_)
          {
            terminal_.setDefaultWorkingDirectory(config_.dataPath);
            (void)terminal_.addSession(config_.dataPath);
          }
          else
          {
            terminal_new_tab_on_open_ = true;
            request_open_terminal_ = true;
          }
        }
        else
        {
          request_open_terminal_ = true;
        }
        continue;
      }
      if(terminal_visible_ && terminal_ctrl && !terminal_alt && event.key.keysym.sym == SDLK_TAB)
      {
        (void)terminal_.selectAdjacentSession(terminal_shift);
        continue;
      }
    }

    // Handle the core shell controls before any application/dialog shortcut
    // branch can consume the SDL key event. Printable text uses SDL_TEXTINPUT,
    // but arrows, Tab, and Ctrl+R must reach the PTY from SDL_KEYDOWN.
    if(event.type == SDL_KEYDOWN && terminal_visible_)
    {
      const Uint16 terminal_mod = event.key.keysym.mod;
      const bool terminal_ctrl = (terminal_mod & KMOD_CTRL) != 0;
      const bool terminal_shift = (terminal_mod & KMOD_SHIFT) != 0;
      const bool terminal_alt = (terminal_mod & KMOD_ALT) != 0;
      const SDL_Keycode terminal_sym = event.key.keysym.sym;
      if(!terminal_alt && ((terminal_sym == SDLK_UP) || (terminal_sym == SDLK_DOWN) ||
                           (terminal_sym == SDLK_LEFT) || (terminal_sym == SDLK_RIGHT) ||
                           (terminal_sym == SDLK_TAB) ||
                           (terminal_ctrl && terminal_sym == SDLK_r)))
      {
        if(terminal_ctrl && terminal_sym == SDLK_r)
          terminal_.write("\x12");
        else if(terminal_sym == SDLK_TAB)
          terminal_.sendKey(ImGuiKey_Tab, false, terminal_shift, false);
        else if(terminal_sym == SDLK_UP)
          terminal_.sendKey(ImGuiKey_UpArrow, false, terminal_shift, false);
        else if(terminal_sym == SDLK_DOWN)
          terminal_.sendKey(ImGuiKey_DownArrow, false, terminal_shift, false);
        else if(terminal_sym == SDLK_LEFT)
          terminal_.sendKey(ImGuiKey_LeftArrow, false, terminal_shift, false);
        else
          terminal_.sendKey(ImGuiKey_RightArrow, false, terminal_shift, false);
        continue;
      }
    }

    if(event.type == SDL_DROPFILE && event.drop.file)
    {
      terminal_.clearSelection();
      int mx = 0, my = 0;
      SDL_GetMouseState(&mx, &my);
      pending_dropped_files_.push_back({std::string(event.drop.file), mx, my});
      SDL_free(event.drop.file);
      continue;
    }

    if(event.type == SDL_QUIT) request_close();
    if(event.type == SDL_WINDOWEVENT &&
       event.window.event == SDL_WINDOWEVENT_CLOSE &&
       event.window.windowID == SDL_GetWindowID(window_))
    {
      request_close();
    }

    if(event.type == SDL_WINDOWEVENT &&
       event.window.windowID == SDL_GetWindowID(window_))
    {
      const auto we = event.window.event;
      if(we == SDL_WINDOWEVENT_SIZE_CHANGED || we == SDL_WINDOWEVENT_MOVED ||
         we == SDL_WINDOWEVENT_MAXIMIZED || we == SDL_WINDOWEVENT_RESTORED)
      {
        // Debounced profile check after any window geometry change
        static constexpr int kProfileCheckDelay = 12;
        window_profile_check_delay_ = kProfileCheckDelay;
        window_profile_check_pending_ = false;
      }
      // On restore, ImGui's internal note positions may have been corrupted while
      // the window was minimized (Windows collapses the client area to 0x0, causing
      // ImGui to clamp all windows). Force a layout restore so notes snap back to
      // their saved positions before any position-save logic can run.
      if(we == SDL_WINDOWEVENT_RESTORED)
        force_note_layout_restore_ = true;
    }

    if(git_sync_in_progress_)
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      continue;
    }

    if(event.type == SDL_MOUSEBUTTONDOWN)
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      ImVec2 mouse_pos(static_cast<float>(event.button.x), static_cast<float>(event.button.y));
      if((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
      {
        int window_x = 0;
        int window_y = 0;
        if(SDL_Window *event_window = SDL_GetWindowFromID(event.button.windowID))
        {
          SDL_GetWindowPosition(event_window, &window_x, &window_y);
          mouse_pos.x += static_cast<float>(window_x);
          mouse_pos.y += static_cast<float>(window_y);
        }
      }
      ImGuiWindow *hovered_window = nullptr;
      ImGuiWindow *hovered_window_under_moving_window = nullptr;
      ImGui::FindHoveredWindowEx(mouse_pos, true, &hovered_window,
                                 &hovered_window_under_moving_window);
      search_dialog_mouse_focus_override_valid = true;
      search_dialog_mouse_focus_override = search_dialog_contains_window(hovered_window);
      note_switcher_mouse_focus_override_valid = true;
      note_switcher_mouse_focus_override = note_switcher_contains_window(hovered_window);
      terminal_mouse_focus_override_valid = true;
      terminal_mouse_focus_override = terminal_visible_ && terminal_contains_window(hovered_window);
      continue;
    }

    if(event.type == SDL_KEYDOWN &&
       terminal_visible_ && terminal_has_keyboard_focus() &&
       event.key.keysym.sym == SDLK_F6)
    {
      terminal_.releaseFocus();
      request_focus_active_note_ = true;
      continue;
    }

    if(event.type == SDL_KEYDOWN &&
       (note_header_create_name_visible_ || note_color_set_visible_) &&
       !command_finder_window_visible_ &&
       !(terminal_visible_ && terminal_has_keyboard_focus()))
    {
      if(event.key.keysym.sym == SDLK_ESCAPE)
      {
        request_close_note_header_create_ = note_header_create_name_visible_;
        request_close_note_color_set_ = note_color_set_visible_;
        continue;
      }
      if(event.key.repeat == 0 &&
         (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER))
      {
        request_note_header_create_confirm_ = note_header_create_name_visible_;
        request_note_color_set_confirm_ = note_color_set_visible_;
        continue;
      }
      if(event.key.repeat == 0 && note_color_set_visible_ && event.key.keysym.sym == SDLK_UP)
      {
        --note_color_set_navigation_delta_;
        continue;
      }
      if(event.key.repeat == 0 && note_color_set_visible_ && event.key.keysym.sym == SDLK_DOWN)
      {
        ++note_color_set_navigation_delta_;
        continue;
      }
      ImGui_ImplSDL2_ProcessEvent(&event);
      continue;
    }

    // These dialogs own application shortcuts while they are visible. Forward
    // unhandled keys to ImGui so their filter fields still accept text editing.
    if(event.type == SDL_KEYDOWN &&
       !(terminal_visible_ && terminal_has_keyboard_focus()) &&
       (command_finder_window_visible_ || editor_actions_window_visible_))
    {
      if(event.key.keysym.sym == SDLK_ESCAPE)
      {
        request_close_command_finder_ = command_finder_window_visible_;
        request_close_editor_actions_ = editor_actions_window_visible_;
        continue;
      }
      if(event.key.repeat == 0 && event.key.keysym.sym == SDLK_UP)
      {
        if(command_finder_window_visible_)
        {
          if(note_color_set_visible_)
            --note_color_set_navigation_delta_;
          else
            --command_finder_navigation_delta_;
        }
        else
          --editor_actions_navigation_delta_;
        continue;
      }
      if(event.key.repeat == 0 && event.key.keysym.sym == SDLK_DOWN)
      {
        if(command_finder_window_visible_)
        {
          if(note_color_set_visible_)
            ++note_color_set_navigation_delta_;
          else
            ++command_finder_navigation_delta_;
        }
        else
          ++editor_actions_navigation_delta_;
        continue;
      }
      if(event.key.repeat == 0 &&
         (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER))
      {
        if(command_finder_window_visible_)
          request_activate_command_finder_ = true;
        else
          request_activate_editor_actions_ = true;
        continue;
      }
      ImGui_ImplSDL2_ProcessEvent(&event);
      continue;
    }

    if(event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_ESCAPE &&
       (search_window_visible_ || note_switcher_window_visible_) &&
       !(terminal_visible_ && terminal_has_keyboard_focus()))
    {
      request_close_search_ = search_window_visible_;
      request_close_note_switcher_ = note_switcher_window_visible_;
      continue;
    }

    if(event.type == SDL_KEYDOWN &&
       event.key.repeat == 0 &&
       (event.key.keysym.mod & KMOD_CTRL) != 0 &&
       (event.key.keysym.mod & KMOD_SHIFT) == 0 &&
       event.key.keysym.sym == SDLK_e &&
       !(terminal_visible_ && terminal_has_keyboard_focus()))
    {
      request_open_note_switcher_ = true;
      request_close_search_ = true;
      continue;
    }

    // The search input remains focused while the result highlight moves, so
    // consume navigation keys before ImGui turns them into text-cursor motion.
    if(search_window_visible_ && search_dialog_has_keyboard_focus() &&
       !(terminal_visible_ && terminal_has_keyboard_focus()) &&
       event.type == SDL_KEYDOWN)
    {
      if(event.key.keysym.sym == SDLK_UP)
      {
        --search_navigation_delta_;
        continue;
      }
      if(event.key.keysym.sym == SDLK_DOWN)
      {
        ++search_navigation_delta_;
        continue;
      }
      if(event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)
      {
        request_search_activate_ = true;
        continue;
      }
    }

    // The switcher's input remains focused while the result highlight moves, so
    // consume navigation keys before ImGui turns them into text-cursor motion.
    if(note_switcher_window_visible_ && note_switcher_has_keyboard_focus() &&
       !(terminal_visible_ && terminal_has_keyboard_focus()) &&
       event.type == SDL_KEYDOWN)
    {
      if(event.key.keysym.sym == SDLK_UP)
      {
        --note_switcher_navigation_delta_;
        continue;
      }
      if(event.key.keysym.sym == SDLK_DOWN)
      {
        ++note_switcher_navigation_delta_;
        continue;
      }
      if((event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) &&
         (event.key.keysym.mod & KMOD_CTRL) == 0)
      {
        request_note_switcher_activate_ = true;
        continue;
      }
    }

    // While editing a note, swallow editor control shortcuts before ImGui sees them
    // so our grouped history handles them instead of InputText's per-character stack.
    if(editing_mode_ &&
       !(terminal_visible_ && terminal_has_keyboard_focus()) &&
       event.type == SDL_KEYDOWN)
    {
      const SDL_Keycode edit_key_sym = event.key.keysym.sym;
      const Uint16 edit_key_mod = event.key.keysym.mod;
      const bool edit_ctrl_down = (edit_key_mod & KMOD_CTRL) != 0;
      const bool edit_shift_down = (edit_key_mod & KMOD_SHIFT) != 0;
      const bool edit_find_shortcut = edit_ctrl_down && !edit_shift_down && edit_key_sym == SDLK_f;
      const bool edit_find_project_shortcut = edit_ctrl_down && edit_shift_down && edit_key_sym == SDLK_f;
      const bool edit_undo_shortcut = edit_ctrl_down && !edit_shift_down && edit_key_sym == SDLK_z;
      const bool edit_redo_shortcut = edit_ctrl_down && (edit_key_sym == SDLK_y || (edit_shift_down && edit_key_sym == SDLK_z));
      const bool edit_editor_actions_shortcut =
          edit_ctrl_down && !edit_shift_down &&
          (edit_key_sym == SDLK_RETURN || edit_key_sym == SDLK_KP_ENTER);
      if(edit_key_sym == SDLK_ESCAPE)
      {
        if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open_requested)
        {
          editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
          request_close_editor_action_ui_widget_chooser_ = false;
        }
        else if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open)
        {
          request_close_editor_action_ui_widget_chooser_ = true;
        }
        else
        {
          request_exit_edit_mode_ = true;
        }
        continue;
      }
      if(edit_editor_actions_shortcut)
      {
        if(event.key.repeat == 0)
        {
          request_open_editor_actions_ = true;
          request_close_note_switcher_ = true;
        }
        continue;
      }
      if(edit_find_project_shortcut)
      {
        request_open_project_search_ = true;
        request_open_search_ = false;
        continue;
      }
      if(edit_find_shortcut)
      {
        request_open_search_ = true;
        request_open_project_search_ = false;
        continue;
      }
      if(edit_undo_shortcut)
      {
        request_undo_edit_ = true;
        continue;
      }
      if(edit_redo_shortcut)
      {
        request_redo_edit_ = true;
        continue;
      }
      if(edit_ctrl_down && !edit_shift_down && edit_key_sym == SDLK_l)
      {
        request_select_line_ = true;
        continue;
      }
      if(edit_ctrl_down && !edit_shift_down && edit_key_sym == SDLK_PERIOD)
      {
#if defined(_WIN32)
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_LWIN;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_OEM_PERIOD;
        inputs[2] = inputs[1];
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3] = inputs[0];
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
#else
        show_emoji_picker_ = !show_emoji_picker_;
        if(show_emoji_picker_) emoji_picker_.reset_search();
#endif
        continue;
      }
    }

    // Keep a copy of composed text while still forwarding the event to
    // ImGui. At the end of frame_ui(), current-frame focus decides whether
    // the terminal receives this text; otherwise the active note InputText
    // handles it normally. SDL_TEXTINPUT preserves layout, Unicode, IME,
    // and paste data.
    if(event.type == SDL_TEXTINPUT && terminal_visible_)
      pending_terminal_text_.append(event.text.text);

    ImGui_ImplSDL2_ProcessEvent(&event);
    const bool imgui_wants_keyboard = ImGui::GetIO().WantCaptureKeyboard || ImGui::GetIO().WantTextInput;
    const bool imgui_wants_text_input = ImGui::GetIO().WantTextInput;
    const bool is_keydown = event.type == SDL_KEYDOWN;
    const SDL_Keycode key_sym = is_keydown ? event.key.keysym.sym : SDLK_UNKNOWN;
    const Uint16 key_mod = is_keydown ? event.key.keysym.mod : static_cast<Uint16>(KMOD_NONE);
    const bool ctrl_down = (key_mod & KMOD_CTRL) != 0;
    const bool shift_down = (key_mod & KMOD_SHIFT) != 0;
    const bool alt_down = (key_mod & KMOD_ALT) != 0;
    const bool undo_shortcut = ctrl_down && !shift_down && key_sym == SDLK_z;
    const bool redo_shortcut = ctrl_down && (key_sym == SDLK_y || (shift_down && key_sym == SDLK_z));

    // Forward terminal keydowns at the SDL event boundary. The terminal is a
    // custom renderer, not an ImGui input widget; polling IsKeyPressed later
    // in frame_ui() loses events when focus changes in the same frame and does
    // not provide reliable shell key repeat.
    if(event.type == SDL_KEYDOWN && terminal_visible_ && terminal_has_keyboard_focus())
    {
      const bool clipboard_shortcut =
          (!alt_down && ctrl_down && shift_down && (key_sym == SDLK_c || key_sym == SDLK_v)) ||
          (!alt_down && ctrl_down && !shift_down && key_sym == SDLK_INSERT) ||
          (!alt_down && !ctrl_down && shift_down && key_sym == SDLK_INSERT);
      if(!clipboard_shortcut)
      {
        ImGuiKey terminal_key = ImGuiKey_None;
        switch(key_sym)
        {
        case SDLK_TAB: terminal_key = ImGuiKey_Tab; break;
        case SDLK_LEFT: terminal_key = ImGuiKey_LeftArrow; break;
        case SDLK_RIGHT: terminal_key = ImGuiKey_RightArrow; break;
        case SDLK_UP: terminal_key = ImGuiKey_UpArrow; break;
        case SDLK_DOWN: terminal_key = ImGuiKey_DownArrow; break;
        case SDLK_PAGEUP: terminal_key = ImGuiKey_PageUp; break;
        case SDLK_PAGEDOWN: terminal_key = ImGuiKey_PageDown; break;
        case SDLK_HOME: terminal_key = ImGuiKey_Home; break;
        case SDLK_END: terminal_key = ImGuiKey_End; break;
        case SDLK_INSERT: terminal_key = ImGuiKey_Insert; break;
        case SDLK_DELETE: terminal_key = ImGuiKey_Delete; break;
        case SDLK_BACKSPACE: terminal_key = ImGuiKey_Backspace; break;
        case SDLK_RETURN: terminal_key = ImGuiKey_Enter; break;
        case SDLK_KP_ENTER: terminal_key = ImGuiKey_KeypadEnter; break;
        case SDLK_ESCAPE: terminal_key = ImGuiKey_Escape; break;
        case SDLK_F1: terminal_key = ImGuiKey_F1; break;
        case SDLK_F2: terminal_key = ImGuiKey_F2; break;
        case SDLK_F3: terminal_key = ImGuiKey_F3; break;
        case SDLK_F4: terminal_key = ImGuiKey_F4; break;
        case SDLK_F5: terminal_key = ImGuiKey_F5; break;
        case SDLK_F6: terminal_key = ImGuiKey_F6; break;
        case SDLK_F7: terminal_key = ImGuiKey_F7; break;
        case SDLK_F8: terminal_key = ImGuiKey_F8; break;
        case SDLK_F9: terminal_key = ImGuiKey_F9; break;
        case SDLK_F10: terminal_key = ImGuiKey_F10; break;
        case SDLK_F11: terminal_key = ImGuiKey_F11; break;
        case SDLK_F12: terminal_key = ImGuiKey_F12; break;
        default:
          if(key_sym >= SDLK_a && key_sym <= SDLK_z)
            terminal_key = static_cast<ImGuiKey>(ImGuiKey_A + (key_sym - SDLK_a));
          break;
        }
        if(terminal_key != ImGuiKey_None)
        {
          const bool alt = (key_mod & KMOD_ALT) != 0;
          if(ctrl_down && !alt && key_sym >= SDLK_a && key_sym <= SDLK_z)
          {
            const char control = static_cast<char>(key_sym - SDLK_a + 1);
            terminal_.write(std::string_view(&control, 1));
          }
          else if(!ctrl_down && !alt)
          {
            switch(key_sym)
            {
            case SDLK_TAB: terminal_.write(shift_down ? "\x1b[Z" : "\t"); break;
            case SDLK_LEFT: terminal_.write("\x1b[D"); break;
            case SDLK_RIGHT: terminal_.write("\x1b[C"); break;
            case SDLK_UP: terminal_.write("\x1b[A"); break;
            case SDLK_DOWN: terminal_.write("\x1b[B"); break;
            case SDLK_PAGEUP: terminal_.write("\x1b[5~"); break;
            case SDLK_PAGEDOWN: terminal_.write("\x1b[6~"); break;
            case SDLK_HOME: terminal_.write("\x1b[H"); break;
            case SDLK_END: terminal_.write("\x1b[F"); break;
            case SDLK_DELETE: terminal_.write("\x1b[3~"); break;
            case SDLK_BACKSPACE: terminal_.write("\x7f"); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: terminal_.write("\r"); break;
            case SDLK_ESCAPE: terminal_.write("\x1b"); break;
            default: terminal_.sendKey(terminal_key, false, shift_down, false); break;
            }
          }
          else
          {
            terminal_.sendKey(terminal_key, ctrl_down, shift_down, alt);
          }
        }
      }
      continue;
    }

    // Ctrl+Shift+P opens the workspace command finder. A focused terminal
    // remains an isolated input surface and receives this chord normally.
    if(event.type == SDL_KEYDOWN &&
       event.key.repeat == 0 &&
       ctrl_down &&
       shift_down &&
       event.key.keysym.sym == SDLK_p &&
       !(terminal_visible_ && terminal_has_keyboard_focus()))
    {
      request_open_command_finder_ = true;
      continue;
    }
    // The focused terminal owns all other key presses, including controls
    // that are also workspace shortcuts (for example Ctrl+F and Ctrl+L).
    if(event.type == SDL_KEYDOWN && terminal_visible_ && terminal_has_keyboard_focus())
      continue;
    if(event.type == SDL_KEYDOWN && (command_finder_window_visible_ || editor_actions_window_visible_))
      continue;
    if(event.type == SDL_KEYDOWN && event.key.repeat == 0 && ctrl_down && !shift_down && key_sym == SDLK_n)
    {
      request_new_note_ = true;
      continue;
    }
    if(event.type == SDL_KEYDOWN && event.key.repeat == 0 && ctrl_down && !shift_down && key_sym == SDLK_w)
    {
      request_hide_focused_note_ = true;
      continue;
    }
    if(event.type == SDL_KEYDOWN && event.key.repeat == 0 && ctrl_down && shift_down && key_sym == SDLK_w)
    {
      request_hide_folder_notes_ = true;
      continue;
    }
    if(event.type == SDL_KEYDOWN && event.key.repeat == 0 && ctrl_down && key_sym == SDLK_TAB)
    {
      request_cycle_visible_notes_ = shift_down ? -1 : 1;
      continue;
    }
    if(!editing_mode_ &&
       has_active_note() &&
       event.type == SDL_KEYDOWN &&
       event.key.repeat == 0 &&
       ctrl_down &&
       !shift_down &&
       (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER))
    {
      editing_mode_ = true;
      request_close_note_switcher_ = true;
      request_focus_active_note_ = true;
      request_focus_editor_ = true;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       ctrl_down &&
       !shift_down &&
       event.key.keysym.sym == SDLK_f)
    {
      request_open_search_ = true;
      request_open_project_search_ = false;
      continue;
    }
    if(!editing_mode_ &&
       event.type == SDL_KEYDOWN &&
       ctrl_down &&
       shift_down &&
       event.key.keysym.sym == SDLK_f)
    {
      request_open_project_search_ = true;
      request_open_search_ = false;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_keyboard &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_ESCAPE)
    {
      request_clear_selection_ = true;
      request_cancel_draw_tools_ = true;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_text_input &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_F2)
    {
      request_rename_selected_ = true;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_keyboard &&
       event.type == SDL_KEYDOWN &&
       event.key.keysym.sym == SDLK_DELETE)
    {
      request_delete_selected_ = true;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_text_input &&
       event.type == SDL_KEYDOWN &&
       undo_shortcut)
    {
      request_undo_edit_ = true;
      request_undo_draw_ = true;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_text_input &&
       event.type == SDL_KEYDOWN &&
       redo_shortcut)
    {
      request_redo_edit_ = true;
      request_redo_draw_ = true;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_keyboard &&
       event.type == SDL_KEYDOWN &&
       ctrl_down &&
       event.key.keysym.sym == SDLK_c)
    {
      request_copy_sidebar_ = true;
      continue;
    }
    if(!editing_mode_ &&
       !imgui_wants_keyboard &&
       event.type == SDL_KEYDOWN &&
       ctrl_down &&
       event.key.keysym.sym == SDLK_v)
    {
      request_paste_sidebar_ = true;
      continue;
    }
  }

  if(close_requested_) advance_close();

  // Borderless window drag (update position while mouse held)
  if(window_drag_active_)
  {
    int mx = 0, my = 0;
    SDL_GetGlobalMouseState(&mx, &my);
    const Uint32 mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
    if(mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT))
    {
      SDL_SetWindowPosition(window_,
                            window_drag_start_wx_ + (mx - window_drag_start_mx_),
                            window_drag_start_wy_ + (my - window_drag_start_my_));
    }
    else
    {
      if(window_drag_was_maximized_)
      {
        int display_index = display_index_for_point(mx, my);
        if(display_index < 0) display_index = display_index_for_window(window_);

        SDL_Rect bounds{};
        if(display_index >= 0 && get_display_bounds(display_index, bounds))
        {
          apply_borderless_maximized_window(window_, bounds.x, bounds.y, bounds.w, bounds.h);
          if(!git_sync_in_progress_ && !close_requested_)
          {
            capture_to_active_profile();
            save_profiles();
            save_index();
            window_profile_check_pending_ = false;
            window_profile_check_delay_ = 0;
          }
          else
          {
            window_profile_check_pending_ = true;
          }
        }
      }
      window_drag_active_ = false;
      window_drag_was_maximized_ = false;
    }
  }

  // Profile auto-switch settle countdown and trigger
  if(window_profile_check_delay_ > 0)
  {
    if(--window_profile_check_delay_ == 0)
      window_profile_check_pending_ = true;
  }
  if(window_profile_check_pending_)
  {
    window_profile_check_pending_ = false;
    do_window_profile_switch();
  }

  flush_pending_note_fonts();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  return had_event;
}
void App::frame_ui()
{
  frame_ui_active_ = true;
  struct FrameUiGuard
  {
    App &app;
    ~FrameUiGuard()
    {
      app.frame_ui_active_ = false;
      if(!app.state_dirty_) app.flush_pending_command_mutations();
    }
  } frame_ui_guard{*this};
  MarkdownView::begin_sidebar_thumbnail_frame();
  const bool git_disabled_for_frame = git_sync_in_progress_ || close_requested_;
  const auto set_git_sync_enabled = [this](const bool requested) {
    git_sync_enabled_ = requested;
    project_settings_dirty_ = true;
    project_settings_error_.clear();
    if(git_sync_enabled_) begin_git_operation(false);
  };
  if(git_disabled_for_frame) ImGui::BeginDisabled();

  // --- Dock host (workspace only: right pane, excluding explorer and top bar) ---
  ImGuiViewport *vp = ImGui::GetMainViewport();
  const float explorer_w = 280.0f;
  const bool dock_drag_active = GImGui->MovingWindow != nullptr;
  const float workspace_top_h = folder_overview_mode_ ? 32.0f : 0.0f;
  const ImVec2 workspace_pos(vp->Pos.x + explorer_w, vp->Pos.y + workspace_top_h);
  const ImVec2 workspace_size(
      std::max(200.0f, vp->Size.x - explorer_w),
      std::max(100.0f, vp->Size.y - workspace_top_h));
  ImGui::SetNextWindowPos(workspace_pos);
  ImGui::SetNextWindowSize(workspace_size);
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
  if(request_focus_sidebar_)
  {
    ImGui::SetNextWindowFocus();
    request_focus_sidebar_ = false;
  }
  ImGui::Begin(
      "Explorer",
      nullptr,
      ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking |
          ImGuiWindowFlags_NoTitleBar |
          (dock_drag_active ? ImGuiWindowFlags_NoInputs : 0));
  ImGui::PopStyleColor();
  // Custom Explorer header: label on the left, refresh button on the right.
  static bool request_sync_files = false;
  static bool open_restore_bak_popup = false;
  static std::vector<std::string> bak_candidates;
  static std::vector<int> bak_selected;
  {
    const float btn_sz = 14.0f;
    const float btn_gap = 4.0f;
    const float right_margin = ImGui::GetStyle().WindowPadding.x;
    const ImTextureID refresh_icon = get_toolbar_icon_texture("refresh.png");
    const ImTextureID recycle_icon = get_toolbar_icon_texture("recycle.png");
    const ImTextureID sync_icon = get_toolbar_icon_texture("sync.png");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Lang::t("Explorer"));
    if(!g_unresolved_persistence_errors.empty())
    {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.25f, 1.0f), "!");
      if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      {
        const auto &error = *g_unresolved_persistence_errors.begin();
        ImGui::SetTooltip("Project persistence error\n%s\n%s", error.first.c_str(),
                          error.second.c_str());
      }
    }
    const ImVec2 git_switch_size(30.0f, 16.0f);
    const float git_switch_x = ImGui::GetWindowWidth() - right_margin -
                               2.0f * btn_sz - 2.0f * btn_gap - git_switch_size.x;
    if(git_sync_enabled_)
    {
      ImGui::SameLine(0.0f, 6.0f);
      ImGui::SetCursorPosX(git_switch_x - btn_gap - btn_sz);
      if(shaded_icon_button("##explorer_git_sync_now", sync_icon, ImVec2(btn_sz, btn_sz), "S##explorer_git_sync_now"))
        begin_git_operation(true);
      if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", Lang::t("Sync now"));
    }
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetCursorPosX(git_switch_x);
    ImGui::InvisibleButton("##explorer_git_sync_switch", git_switch_size);
    const bool git_switch_clicked = ImGui::IsItemClicked();
    const bool git_switch_hovered = ImGui::IsItemHovered();
    const ImVec2 git_switch_min = ImGui::GetItemRectMin();
    const ImVec2 git_switch_max = ImGui::GetItemRectMax();
    const float git_switch_radius = git_switch_size.y * 0.5f;
    const ImU32 git_switch_track = ImGui::GetColorU32(
        git_sync_enabled_ ? ImGuiCol_Button : ImGuiCol_TextDisabled);
    const ImU32 git_switch_knob = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    ImDrawList *git_switch_draw_list = ImGui::GetWindowDrawList();
    git_switch_draw_list->AddRectFilled(
        git_switch_min, git_switch_max, git_switch_track, git_switch_radius);
    const float git_switch_knob_x = git_sync_enabled_ ? git_switch_max.x - git_switch_radius
                                                       : git_switch_min.x + git_switch_radius;
    git_switch_draw_list->AddCircleFilled(
        ImVec2(git_switch_knob_x, git_switch_min.y + git_switch_radius),
        git_switch_radius - 2.0f, git_switch_knob);
    if(git_switch_clicked) set_git_sync_enabled(!git_sync_enabled_);
    if(git_switch_hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("Git Sync: %s", git_sync_enabled_ ? "enabled" : "disabled");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btn_sz - right_margin);
    if(shaded_icon_button("##explorer_refresh", refresh_icon, ImVec2(btn_sz, btn_sz), "R##explorer_refresh"))
    {
      request_sync_files = true;
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", Lang::t("refresh_tooltip"));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2.0f * btn_sz - btn_gap - right_margin);
    if(shaded_icon_button("##explorer_recycle", recycle_icon, ImVec2(btn_sz, btn_sz), "~##explorer_recycle"))
    {
      namespace fs = std::filesystem;
      bak_candidates.clear();
      bak_selected.clear();
      std::error_code ec;
      for(const auto &entry : fs::recursive_directory_iterator(config_.dataPath, ec))
      {
        if(!entry.is_regular_file(ec)) continue;
        const std::string p = entry.path().string();
        if(p.size() > 4 && p.compare(p.size() - 4, 4, ".bak") == 0)
        {
          bak_candidates.push_back(p);
          bak_selected.push_back(true);
        }
      }
      open_restore_bak_popup = true;
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", Lang::t("Restore removed notes"));
    ImGui::Separator();
  }
  if(request_sync_files)
  {
    MarkdownView::clear_sidebar_thumbnail_cache();
    sync_project_files();
    request_sync_files = false;
  }
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
  static bool open_rename_image_popup = false;
  static int rename_image_folder_idx = -1;
  static std::string rename_image_current_path;
  static char rename_image_buf[256] = {};
  static int pending_move_image_src_fi = -1;
  static int pending_move_image_dst_fi = -1;
  static std::string pending_move_image_path;
  static bool sidebar_last_selected_was_folder = false;
  static char rename_folder_buf[256] = {};
  static bool open_note_color_popup = false;
  static int color_note_folder_idx = -1;
  static int color_note_idx = -1;
  static float note_color_buf[3] = {0.0f, 0.0f, 0.0f};
  static bool note_color_use_default = true;
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
  enum class SearchScope
  {
    CurrentEditorNote,
    SelectedPreviewNotes,
    CurrentPageNotes,
    FullProject
  };
  struct SearchResult
  {
    int folder_idx = -1;
    int note_idx = -1;
    std::string note_title;
    std::string note_path;
    std::string field_label;
    std::string preview;
    int offset = -1;
    int length = 0;
    int line = 1;
    int column = 1;
    bool content_match = false;
  };
  struct SearchDialogState
  {
    bool visible = false;
    bool focus_input = false;
    bool just_opened = false;
    SearchScope scope = SearchScope::CurrentPageNotes;
    char query[256] = {};
    std::string scope_label;
    std::vector<SearchResult> results;
    int scanned_notes = 0;
    int total_matches = 0;
    bool truncated = false;
    std::string last_query;
    SearchScope last_scope = SearchScope::CurrentPageNotes;
  };
  static SearchDialogState search_dialog;
  struct NoteSwitcherResult
  {
    std::string note_id;
    int folder_idx = -1;
    int note_idx = -1;
    std::string note_title;
    std::string folder_name;
    std::string note_path;
    std::string heading_path;
    std::string heading_title;
    std::size_t heading_occurrence_index = 0;
    int score = 0;
  };
  struct NoteSwitcherState
  {
    bool visible = false;
    bool focus_input = false;
    bool just_opened = false;
    char query[256] = {};
    std::vector<NoteSwitcherResult> results;
    std::string last_query;
  };
  static NoteSwitcherState note_switcher;
  enum class CommandFinderCommand
  {
    terminal,
    focus_active_note,
    new_note,
    hide_focused_note,
    hide_folder_notes,
    quick_open,
    find_current_note,
    find_in_project,
    api_command
  };
  struct CommandFinderState
  {
    bool visible = false;
    bool focus_input = false;
    char query[128] = {};
    int selected = 0;
  };
  static CommandFinderState command_finder;
  static std::string deferred_sidebar_snapshot_before;

  if(reset_sidebar_state_)
  {
    request_sync_files = false;
    new_folder_buf[0] = '\0';
    new_note_buf[0] = '\0';
    open_new_folder_popup = false;
    open_new_note_popup = false;
    focus_new_note_input = false;
    new_folder_parent_idx = -1;
    new_note_target_folder_idx = -1;
    force_open_folder_idx = -1;
    open_rename_note_popup = false;
    rename_note_folder_idx = -1;
    rename_note_idx = -1;
    rename_note_buf[0] = '\0';
    open_rename_folder_popup = false;
    rename_folder_idx = -1;
    open_rename_image_popup = false;
    rename_image_folder_idx = -1;
    rename_image_current_path.clear();
    rename_image_buf[0] = '\0';
    pending_move_image_src_fi = -1;
    pending_move_image_dst_fi = -1;
    pending_move_image_path.clear();
    sidebar_last_selected_was_folder = false;
    rename_folder_buf[0] = '\0';
    open_note_color_popup = false;
    color_note_folder_idx = -1;
    color_note_idx = -1;
    note_color_buf[0] = note_color_buf[1] = note_color_buf[2] = 0.0f;
    note_color_use_default = true;
    pending_delete_folder_idx = -1;
    pending_delete_note_folder_idx = -1;
    pending_delete_note_idx = -1;
    pending_delete_note_indices.clear();
    pending_paste_note_folder_idx = -1;
    paste_target_folder_idx = -1;
    open_paste_note_popup = false;
    paste_note_buf[0] = '\0';
    selected_note_indices.clear();
    selected_stroke_indices.clear();
    pending_focus_note_idx = -1;
    last_sidebar_anchor_folder_idx = -1;
    last_sidebar_anchor_note_idx = -1;
    pending_move_source_folder_idx = -1;
    pending_move_target_folder_idx = -1;
    pending_move_note_indices.clear();
    pending_move_folder_source_idx = -1;
    pending_move_folder_target_idx = -1;
    drag_hover_folder_idx = -1;
    sidebar_flashes.clear();
    search_dialog = {};
    search_selected_idx_ = -1;
    search_navigation_delta_ = 0;
    request_search_activate_ = false;
    note_switcher = {};
    note_switcher_selected_idx_ = -1;
    note_switcher_navigation_delta_ = 0;
    request_open_note_switcher_ = false;
    request_note_switcher_activate_ = false;
    note_switcher_window_visible_ = false;
    request_close_note_switcher_ = false;
    note_header_create_selecting_ = false;
    note_header_create_name_visible_ = false;
    request_note_header_create_confirm_ = false;
    request_close_note_header_create_ = false;
    note_header_create_focus_input_ = false;
    note_header_create_title_[0] = '\0';
    note_header_create_note_reference_.clear();
    note_header_create_parent_.clear();
    note_header_create_parent_path_.clear();
    note_header_create_parent_occurrence_ = 0;
    note_color_set_selecting_ = false;
    note_color_set_visible_ = false;
    request_note_color_set_confirm_ = false;
    request_close_note_color_set_ = false;
    note_color_set_focus_input_ = false;
    note_color_set_color_[0] = 0.35f;
    note_color_set_color_[1] = 0.65f;
    note_color_set_color_[2] = 1.0f;
    note_color_set_selected_preset_ = 3;
    note_color_set_navigation_delta_ = 0;
    note_color_set_note_reference_.clear();
    command_finder = {};
    command_finder_window_visible_ = false;
    request_open_command_finder_ = false;
    request_close_command_finder_ = false;
    request_activate_command_finder_ = false;
    command_finder_navigation_delta_ = 0;
    editor_actions_window_visible_ = false;
    request_open_editor_actions_ = false;
    request_close_editor_actions_ = false;
    request_activate_editor_actions_ = false;
    editor_actions_navigation_delta_ = 0;
    deferred_sidebar_snapshot_before.clear();
    open_restore_bak_popup = false;
    bak_candidates.clear();
    bak_selected.clear();
    reset_sidebar_state_ = false;
  }

  auto remove_pending_delete_path = [&](const std::string &path) {
    if(path.empty()) return;
    const std::filesystem::path backup(path + ".bak");
    std::error_code exists_error;
    const bool has_backup = std::filesystem::exists(backup, exists_error);
    if(exists_error || (has_backup && !restore_project_backup(path))) return;
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
  auto to_lower_ascii = [](std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for(char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
  };
  auto make_search_preview = [](std::string_view text, size_t match_pos, size_t match_len) {
    match_pos = std::min(match_pos, text.size());
    const size_t previous_line_break = match_pos == 0 ? std::string_view::npos : text.find_last_of("\r\n", match_pos - 1);
    const size_t line_start = previous_line_break == std::string_view::npos ? 0 : previous_line_break + 1;
    const size_t next_line_break = text.find_first_of("\r\n", match_pos);
    const size_t line_end = next_line_break == std::string_view::npos ? text.size() : next_line_break;

    constexpr size_t kContextBefore = 36;
    constexpr size_t kContextAfter = 56;
    const size_t start = std::max(line_start, match_pos > kContextBefore ? match_pos - kContextBefore : 0);
    const size_t match_end = std::min(line_end, match_pos + std::max<size_t>(match_len, 1));
    const size_t end = std::min(line_end, match_end + std::min(kContextAfter, line_end - match_end));

    std::string out;
    if(start > line_start) out += "...";
    out.append(text.substr(start, end - start));
    if(end < line_end) out += "...";
    for(char &c : out)
    {
      if(c == '\t') c = ' ';
    }
    return out;
  };
  auto compute_line_col = [](std::string_view text, size_t match_pos, int &line_out, int &column_out) {
    line_out = 1;
    column_out = 1;
    const size_t limit = std::min(match_pos, text.size());
    for(size_t i = 0; i < limit; ++i)
    {
      if(text[i] == '\n')
      {
        ++line_out;
        column_out = 1;
      }
      else
      {
        ++column_out;
      }
    }
  };
  auto scope_label_for = [&](SearchScope scope) {
    switch(scope)
    {
    case SearchScope::CurrentEditorNote:
      return std::string("Current note");
    case SearchScope::SelectedPreviewNotes:
      return std::string("Selected notes");
    case SearchScope::CurrentPageNotes:
      return std::string("Current page");
    case SearchScope::FullProject:
      return std::string("All folders and notes");
    }
    return std::string("Search");
  };
  auto navigate_to_search_result = [&](const SearchResult &result, bool prefer_edit) {
    if(result.folder_idx < 0 || result.folder_idx >= (int)folders_.size()) return;
    const FolderMeta &target_folder = folders_[(size_t)result.folder_idx];
    if(result.note_idx < 0 || result.note_idx >= (int)target_folder.notes.size()) return;

    if(folder_overview_mode_)
    {
      const int prev_folder = active_folder_idx_;
      flush_pending_text_history();
      active_folder_idx_ = result.folder_idx;
      active_note_idx_ = result.note_idx;
      folder_overview_mode_ = true;
      selected_note_indices.clear();
      selected_note_indices.insert(result.note_idx);
      selected_stroke_indices.clear();
      pending_focus_note_idx = result.note_idx;
      if(result.folder_idx != prev_folder)
        apply_folder_settings(result.folder_idx);
      load_note_content_for_active();
      save_index();
    }
    else
    {
      set_active_note(result.folder_idx, result.note_idx);
      editing_mode_ = prefer_edit;
    }

    search_jump_note_path_ = result.note_path;
    search_jump_pos_ = result.offset;
    search_jump_len_ = result.length;
    search_jump_force_edit_ = prefer_edit && result.content_match && result.offset >= 0;
    if(!prefer_edit && result.content_match && result.offset >= 0)
      request_preview_source_offset(result.note_path, static_cast<std::size_t>(result.offset));
    search_request_window_focus_ = prefer_edit;
  };
  auto highlight_current_editor_match = [&](const SearchResult &result) {
    if(!result.content_match || result.offset < 0 || result.note_path != state_file_path_) return;
    // Scroll the inactive multiline child directly. Activating the editor here
    // would steal the keyboard focus needed for repeated search navigation.
    const int start = std::min(result.offset, static_cast<int>(markdown_text_.size()));
    const int end = std::min(start + std::max(1, result.length), static_cast<int>(markdown_text_.size()));
    search_editor_scroll_note_path_ = result.note_path;
    search_editor_scroll_pos_ = start;
    search_editor_selection_note_path_ = result.note_path;
    search_editor_selection_start_ = start;
    search_editor_selection_end_ = end;
    search_editor_match_note_path_ = result.note_path;
    search_editor_match_offset_ = start;
    search_editor_match_length_ = end - start;
    search_editor_selection_focus_pending_ = true;
    search_request_window_focus_ = false;
    search_dialog.focus_input = true;
  };
  auto refresh_search_results = [&]() {
    search_dialog.results.clear();
    search_dialog.scanned_notes = 0;
    search_dialog.total_matches = 0;
    search_dialog.truncated = false;

    const std::string query_text(search_dialog.query);
    const std::string query_trim = std::string(StringUtils::trim(query_text));
    search_editor_scroll_note_path_.clear();
    search_editor_scroll_pos_ = -1;
    search_editor_selection_note_path_.clear();
    search_editor_selection_start_ = -1;
    search_editor_selection_end_ = -1;
    search_editor_selection_focus_pending_ = false;
    search_editor_match_note_path_.clear();
    search_editor_match_offset_ = -1;
    search_editor_match_length_ = 0;
    if(query_trim.empty()) return;

    const std::string query_lower = to_lower_ascii(query_trim);
    static constexpr int kMaxSearchResults = 400;

    auto append_matches_in_text = [&](int folder_idx, int note_idx, const std::string &note_title, const std::string &note_path, const std::string &field_label, const std::string &text, bool content_match) {
      const std::string haystack_lower = to_lower_ascii(text);
      size_t pos = haystack_lower.find(query_lower);
      while(pos != std::string::npos)
      {
        SearchResult result;
        result.folder_idx = folder_idx;
        result.note_idx = note_idx;
        result.note_title = note_title;
        result.note_path = note_path;
        result.field_label = field_label;
        result.offset = content_match ? static_cast<int>(pos) : -1;
        result.length = static_cast<int>(query_trim.size());
        result.content_match = content_match;
        if(content_match)
        {
          compute_line_col(text, pos, result.line, result.column);
          result.preview = make_search_preview(text, pos, query_trim.size());
        }
        else
        {
          result.preview = text;
        }

        search_dialog.results.push_back(std::move(result));
        ++search_dialog.total_matches;
        if((int)search_dialog.results.size() >= kMaxSearchResults)
        {
          search_dialog.truncated = true;
          return false;
        }
        pos = haystack_lower.find(query_lower, pos + 1);
      }
      return true;
    };

    auto search_note = [&](int folder_idx, int note_idx, bool include_names) {
      if(folder_idx < 0 || folder_idx >= (int)folders_.size()) return true;
      const FolderMeta &folder = folders_[(size_t)folder_idx];
      if(note_idx < 0 || note_idx >= (int)folder.notes.size()) return true;
      const NoteMeta &note = folder.notes[(size_t)note_idx];
      ++search_dialog.scanned_notes;

      if(include_names)
      {
        if(!append_matches_in_text(folder_idx, note_idx, note.title, note.path, "Title", note.title, false)) return false;
        if(!append_matches_in_text(folder_idx, note_idx, note.title, note.path, "Path", note.path, false)) return false;
      }

      const std::string content = (note.path == state_file_path_) ? markdown_text_ : read_text_file(note.path);
      if(!append_matches_in_text(folder_idx, note_idx, note.title, note.path, "Content", content, true)) return false;
      return true;
    };

    switch(search_dialog.scope)
    {
    case SearchScope::CurrentEditorNote: {
      if(has_active_note()) search_note(active_folder_idx_, active_note_idx_, false);
      break;
    }
    case SearchScope::SelectedPreviewNotes: {
      if(active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
      {
        std::vector<int> indices(selected_note_indices.begin(), selected_note_indices.end());
        if(indices.empty() && has_active_note()) indices.push_back(active_note_idx_);
        std::sort(indices.begin(), indices.end());
        for(int idx : indices)
        {
          if(!search_note(active_folder_idx_, idx, false)) break;
        }
      }
      break;
    }
    case SearchScope::CurrentPageNotes: {
      if(folder_overview_mode_ && active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
      {
        const FolderMeta &folder = folders_[(size_t)active_folder_idx_];
        for(int idx = 0; idx < (int)folder.notes.size(); ++idx)
        {
          if(!search_note(active_folder_idx_, idx, false)) break;
        }
      }
      else if(has_active_note())
      {
        search_note(active_folder_idx_, active_note_idx_, false);
      }
      break;
    }
    case SearchScope::FullProject: {
      for(int fi = 0; fi < (int)folders_.size(); ++fi)
      {
        const FolderMeta &folder = folders_[(size_t)fi];
        for(int ni = 0; ni < (int)folder.notes.size(); ++ni)
        {
          if(!search_note(fi, ni, true)) break;
        }
        if(search_dialog.truncated) break;
      }
      break;
    }
    }
  };
  auto open_search_dialog = [&](SearchScope scope) {
    search_dialog.scope = scope;
    search_dialog.scope_label = scope_label_for(scope);
    search_dialog.visible = true;
    search_dialog.focus_input = true;
    search_dialog.just_opened = true;
    search_selected_idx_ = -1;
    search_navigation_delta_ = 0;
    request_search_activate_ = false;
  };
  auto open_default_search_dialog = [&]() {
    if(request_open_project_search_)
    {
      open_search_dialog(SearchScope::FullProject);
      request_open_project_search_ = false;
    }
    if(request_open_search_)
    {
      SearchScope scope = SearchScope::CurrentPageNotes;
      if(has_active_note()) scope = SearchScope::CurrentEditorNote;
      open_search_dialog(scope);
      request_open_search_ = false;
    }
  };
  auto render_search_dialog = [&]() {
    open_default_search_dialog();
    if(request_close_search_)
    {
      if(search_dialog.scope == SearchScope::CurrentEditorNote)
        search_request_window_focus_ = true;
      search_dialog.visible = false;
      request_close_search_ = false;
      search_selected_idx_ = -1;
      search_navigation_delta_ = 0;
      request_search_activate_ = false;
    }
    search_window_visible_ = search_dialog.visible;
    if(!search_dialog.visible) return;

    const bool just_opened = search_dialog.just_opened;
    const bool editor_search = search_dialog.scope == SearchScope::CurrentEditorNote;
    const ImGuiCond placement_cond = just_opened ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(editor_search ? ImVec2(500.0f, 150.0f) : ImVec2(680.0f, 480.0f),
                             editor_search ? ImGuiCond_Always : placement_cond);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), placement_cond, ImVec2(0.5f, 0.5f));
    if(just_opened) ImGui::SetNextWindowFocus();
    if(!ImGui::Begin(Lang::t("Search"), &search_dialog.visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
    {
      search_dialog.just_opened = false;
      ImGui::End();
      search_window_visible_ = search_dialog.visible;
      if(!search_dialog.visible)
      {
        search_selected_idx_ = -1;
        search_navigation_delta_ = 0;
        request_search_activate_ = false;
      }
      return;
    }
    search_dialog.just_opened = false;

    if(search_dialog.focus_input)
    {
      ImGui::SetKeyboardFocusHere();
      search_dialog.focus_input = false;
    }

    ImGui::SetNextItemWidth(editor_search ? 360.0f : 420.0f);
    const bool query_changed = ImGui::InputText(
        "Query",
        search_dialog.query,
        sizeof(search_dialog.query),
        ImGuiInputTextFlags_AutoSelectAll);

    const std::string current_query(search_dialog.query);
    bool search_selection_moved = false;
    const bool search_results_refreshed =
        query_changed || current_query != search_dialog.last_query ||
        search_dialog.scope != search_dialog.last_scope || just_opened;
    if(search_results_refreshed)
    {
      search_selected_idx_ = -1;
      search_dialog.scope_label = scope_label_for(search_dialog.scope);
      refresh_search_results();
      if(!search_dialog.results.empty())
      {
        search_selected_idx_ = 0;
        search_selection_moved = true;
        if(editor_search)
          highlight_current_editor_match(search_dialog.results.front());
      }
      search_dialog.last_query = current_query;
      search_dialog.last_scope = search_dialog.scope;
    }

    if(!search_dialog.results.empty() && search_navigation_delta_ != 0)
    {
      const int previous_selected_idx = search_selected_idx_;
      const int result_count = static_cast<int>(search_dialog.results.size());
      if(editor_search)
      {
        search_selected_idx_ = (search_selected_idx_ + search_navigation_delta_) % result_count;
        if(search_selected_idx_ < 0) search_selected_idx_ += result_count;
      }
      else
      {
        search_selected_idx_ = clamp_to_range(
            search_selected_idx_ + search_navigation_delta_, 0, result_count - 1);
      }
      search_selection_moved = search_selection_moved ||
                               search_selected_idx_ != previous_selected_idx;
      if(search_selection_moved)
      {
        const SearchResult &selected_result = search_dialog.results[static_cast<std::size_t>(search_selected_idx_)];
        if(editor_search)
          highlight_current_editor_match(selected_result);
      }
    }
    search_navigation_delta_ = 0;

    if(request_search_activate_)
    {
      request_search_activate_ = false;
      if(!search_results_refreshed && search_selected_idx_ >= 0 &&
         search_selected_idx_ < static_cast<int>(search_dialog.results.size()))
      {
        if(editor_search)
        {
          search_selected_idx_ = (search_selected_idx_ + 1) % static_cast<int>(search_dialog.results.size());
          highlight_current_editor_match(search_dialog.results[static_cast<std::size_t>(search_selected_idx_)]);
        }
        else
        {
          navigate_to_search_result(
              search_dialog.results[static_cast<std::size_t>(search_selected_idx_)], false);
          search_dialog.visible = false;
        }
      }
    }

    if(editor_search)
    {
      const std::string_view query = StringUtils::trim(current_query);
      if(query.empty())
        ImGui::TextDisabled("%s", Lang::t("Type to search."));
      else if(search_dialog.results.empty())
        ImGui::TextDisabled("Match 0 / 0 — %s", Lang::t("No matches found."));
      else
        ImGui::TextDisabled("Match %d / %d — Enter: next",
                            search_selected_idx_ + 1,
                            search_dialog.total_matches);

      ImGui::SameLine();
      if(ImGui::Button("Close")) search_dialog.visible = false;
      ImGui::End();
      search_window_visible_ = search_dialog.visible;
      if(!search_dialog.visible)
      {
        search_request_window_focus_ = true;
        search_selected_idx_ = -1;
        search_navigation_delta_ = 0;
        request_search_activate_ = false;
      }
      return;
    }

    ImGui::TextDisabled("Scope: %s", search_dialog.scope_label.c_str());
    ImGui::TextDisabled("Matches: %d across %d notes%s",
                        search_dialog.total_matches,
                        search_dialog.scanned_notes,
                        search_dialog.truncated ? " (truncated)" : "");
    ImGui::Separator();

    ImGui::BeginChild("##search_results", ImVec2(620.0f, 360.0f), true);
    if(StringUtils::trim(current_query).empty())
    {
      ImGui::TextDisabled("%s", Lang::t("Type to search."));
    }
    else if(search_dialog.results.empty())
    {
      ImGui::TextDisabled("%s", Lang::t("No matches found."));
    }
    else
    {
      for(size_t i = 0; i < search_dialog.results.size(); ++i)
      {
        const SearchResult &result = search_dialog.results[i];
        const std::string header =
            result.note_title +
            "  [" + result.field_label + "]" +
            (result.content_match ? "  " + std::to_string(result.line) + ":" + std::to_string(result.column) : "");
        const bool selected = static_cast<int>(i) == search_selected_idx_;
        if(ImGui::Selectable((header + "##search_result_" + std::to_string(i)).c_str(), selected))
        {
          search_selected_idx_ = static_cast<int>(i);
          navigate_to_search_result(result, search_dialog.scope == SearchScope::CurrentEditorNote);
          search_dialog.visible = false;
        }
        if(selected)
        {
          if(search_selection_moved) ImGui::SetScrollHereY(0.5f);
        }
        ImGui::TextDisabled("%s", result.note_path.c_str());
        if(!result.preview.empty()) ImGui::TextWrapped("%s", result.preview.c_str());
        if(i + 1 < search_dialog.results.size()) ImGui::Separator();
      }
    }
    ImGui::EndChild();

    if(ImGui::Button("Close")) search_dialog.visible = false;
    ImGui::End();
    search_window_visible_ = search_dialog.visible;
    if(!search_dialog.visible)
    {
      search_selected_idx_ = -1;
      search_navigation_delta_ = 0;
      request_search_activate_ = false;
    }
  };
  auto navigate_to_note_switcher_result = [&](const NoteSwitcherResult &result) {
    int folder_idx = result.folder_idx;
    int note_idx = result.note_idx;
    if(!result.note_id.empty())
    {
      folder_idx = -1;
      note_idx = -1;
      for(int candidate_folder = 0; candidate_folder < (int)folders_.size(); ++candidate_folder)
      {
        const auto &notes = folders_[(size_t)candidate_folder].notes;
        for(int candidate_note = 0; candidate_note < (int)notes.size(); ++candidate_note)
        {
          if(notes[(size_t)candidate_note].id != result.note_id) continue;
          folder_idx = candidate_folder;
          note_idx = candidate_note;
          break;
        }
        if(folder_idx >= 0) break;
      }
    }
    if(folder_idx < 0 || folder_idx >= (int)folders_.size()) return;
    FolderMeta &folder = folders_[(size_t)folder_idx];
    if(note_idx < 0 || note_idx >= (int)folder.notes.size()) return;

    folder.notes[(size_t)note_idx].hidden = false;
    preview_edit_cursor_note_path_.clear();
    preview_edit_cursor_pos_ = -1;
    request_preview_heading({}, {}, 0);
    request_preview_source_offset({}, 0);
    if(!result.heading_path.empty())
    {
      request_preview_heading(
          result.note_path, result.heading_path, result.heading_occurrence_index);

      const std::string &content = result.note_path == state_file_path_
                                       ? markdown_text_
                                       : read_text_file(result.note_path);
      std::size_t heading_index = 0;
      std::size_t line_start = 0;
      while(line_start < content.size())
      {
        const std::size_t line_end = content.find('\n', line_start);
        const std::size_t end = line_end == std::string::npos ? content.size() : line_end;
        int level = 0;
        std::string_view title;
        if(parse_heading_line(std::string_view(content).substr(line_start, end - line_start), level, title))
        {
          if(heading_index == result.heading_occurrence_index)
          {
            preview_edit_cursor_note_path_ = result.note_path;
            preview_edit_cursor_pos_ = static_cast<int>(line_start);
            break;
          }
          ++heading_index;
        }
        if(line_end == std::string::npos) break;
        line_start = line_end + 1;
      }
    }
    set_active_note(folder_idx, note_idx);
    selected_note_indices.clear();
    selected_note_indices.insert(note_idx);
    selected_stroke_indices.clear();
    pending_focus_note_idx = note_idx;
    force_open_folder_idx = folder_idx;
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    request_cancel_draw_tools_ = true;
    sidebar_last_selected_was_folder = false;
  };
  auto refresh_note_switcher_results = [&]() {
    const int previous_selected_idx = note_switcher_selected_idx_;
    std::string selected_note_id;
    std::string selected_heading_path;
    std::size_t selected_heading_occurrence_index = 0;
    if(previous_selected_idx >= 0 &&
       previous_selected_idx < static_cast<int>(note_switcher.results.size()))
    {
      const NoteSwitcherResult &selected =
          note_switcher.results[static_cast<std::size_t>(previous_selected_idx)];
      selected_note_id = selected.note_id;
      selected_heading_path = selected.heading_path;
      selected_heading_occurrence_index = selected.heading_occurrence_index;
    }

    note_switcher.results.clear();
    static constexpr std::size_t kMaxResults = 400U;
    const std::string query = std::string(StringUtils::trim(note_switcher.query));
    const auto compact_text = [](std::string_view text) {
      std::string compact;
      compact.reserve(text.size());
      for(const char raw : text)
      {
        const unsigned char c = static_cast<unsigned char>(raw);
        if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
          compact.push_back(static_cast<char>(c));
        else if(c >= 'A' && c <= 'Z')
          compact.push_back(static_cast<char>(c - 'A' + 'a'));
      }
      return compact;
    };
    const auto lowercase_text = [](std::string_view text) {
      std::string lowercase;
      lowercase.reserve(text.size());
      for(const char raw : text)
      {
        const unsigned char c = static_cast<unsigned char>(raw);
        lowercase.push_back(
            c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : raw);
      }
      return lowercase;
    };
    const auto split_words = [&](std::string_view text) {
      std::vector<std::string> words;
      std::string word;
      word.reserve(text.size());
      for(const char raw : text)
      {
        const unsigned char c = static_cast<unsigned char>(raw);
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9'))
        {
          word.push_back(
              c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : raw);
        }
        else if(!word.empty())
        {
          words.push_back(std::move(word));
          word.clear();
        }
      }
      if(!word.empty()) words.push_back(std::move(word));
      return words;
    };
    const std::vector<std::string> terms = split_words(query);
    const auto fuzzy_word_match = [](std::string_view word, std::string_view term) {
      if(term.empty() || word.empty() || word.front() != term.front()) return false;

      std::size_t term_index = 1;
      std::size_t word_index = 1;
      while(term_index < term.size() && word_index < word.size())
      {
        if(word[word_index] == term[term_index]) ++term_index;
        ++word_index;
      }
      if(term_index != term.size()) return false;

      // Fuzzy matches must begin at a word boundary and stay compact enough
      // to avoid accepting letters scattered across a long display path.
      return word_index <= term.size() * 2U;
    };
    const auto term_match_quality = [&](std::string_view text, std::string_view term) {
      if(term.empty()) return 0;
      const std::vector<std::string> words = split_words(text);
      for(const std::string &word : words)
      {
        if(word.compare(0, term.size(), term) == 0) return 3;
      }
      const std::string compact = compact_text(text);
      if(compact.find(term) != std::string::npos) return 2;

      for(const std::string &word : words)
      {
        if(fuzzy_word_match(word, term)) return 1;
      }
      return 0;
    };
    const auto match_terms = [&](std::string_view text) {
      if(terms.empty()) return 0;
      int quality = 3;
      for(const std::string &term : terms)
      {
        const int term_quality = term_match_quality(text, term);
        if(term_quality == 0) return 0;
        quality = std::min(quality, term_quality);
      }
      return quality;
    };
    const auto heading_contributes = [&](std::string_view heading_path) {
      for(const std::string &term : terms)
      {
        if(term_match_quality(heading_path, term) != 0) return true;
      }
      return false;
    };
    const std::string normalized_query = compact_text(query);
    const std::string lowercase_query = lowercase_text(query);
    const auto note_score = [&](const NoteMeta &note) {
      if(lowercase_text(note.title) == lowercase_query) return 10000;
      if(compact_text(note.title) == normalized_query) return 9500;

      switch(match_terms(note.title))
      {
      case 3:
        return 9000;
      case 2:
        return 7500;
      case 1:
        return 5500;
      default:
        return 0;
      }
    };
    const auto heading_score = [&](std::string_view display_path) {
      if(lowercase_text(display_path) == lowercase_query) return 7000;
      if(compact_text(display_path) == normalized_query) return 6900;

      switch(match_terms(display_path))
      {
      case 3:
        return 6500;
      case 2:
        return 6000;
      case 1:
        return 5000;
      default:
        return 0;
      }
    };
    const auto append_result = [&](NoteSwitcherResult result) {
      if(note_switcher.results.size() < kMaxResults)
      {
        note_switcher.results.push_back(std::move(result));
        return;
      }

      const auto lowest_score = std::min_element(
          note_switcher.results.begin(), note_switcher.results.end(),
          [](const NoteSwitcherResult &left, const NoteSwitcherResult &right) {
            return left.score < right.score;
          });
      if(lowest_score != note_switcher.results.end() && result.score > lowest_score->score)
        *lowest_score = std::move(result);
    };

    auto append_heading_results = [&](auto &&self,
                                      const std::vector<MdSection> &sections,
                                      const std::vector<std::string> &parent_path,
                                      const NoteMeta &note,
                                      int folder_idx,
                                      int note_idx,
                                      const FolderMeta &folder,
                                      std::size_t &heading_occurrence_index) -> void {
      for(const MdSection &section : sections)
      {
        const std::size_t occurrence_index = heading_occurrence_index++;
        std::vector<std::string> path = parent_path;
        path.push_back(section.title);
        std::string heading_path = path.front();
        for(std::size_t part_index = 1; part_index < path.size(); ++part_index)
          heading_path += "." + path[part_index];

        const std::string display_path = note.title + "." + heading_path;
        const int score = heading_score(display_path);
        if(score != 0 && heading_contributes(heading_path))
        {
          append_result({note.id,
                         folder_idx,
                         note_idx,
                         note.title,
                         folder.name,
                         note.path,
                         heading_path,
                         section.title,
                         occurrence_index,
                         score});
        }
        self(self,
             section.kids,
             path,
             note,
             folder_idx,
             note_idx,
             folder,
             heading_occurrence_index);
      }
    };

    for(int folder_idx = 0; folder_idx < static_cast<int>(folders_.size()); ++folder_idx)
    {
      const FolderMeta &folder = folders_[static_cast<std::size_t>(folder_idx)];
      for(int note_idx = 0; note_idx < static_cast<int>(folder.notes.size()); ++note_idx)
      {
        const NoteMeta &note = folder.notes[static_cast<std::size_t>(note_idx)];
        if(terms.empty())
        {
          append_result({note.id, folder_idx, note_idx, note.title, folder.name, note.path, {}, {}, 0, 0});
          continue;
        }

        const int score = note_score(note);
        if(score != 0)
          append_result({note.id, folder_idx, note_idx, note.title, folder.name, note.path, {}, {}, 0, score});

        const std::string content = (note.path == state_file_path_)
                                        ? markdown_text_
                                        : read_text_file(note.path);
        const MdSection sections = parse_sections(content);
        std::size_t heading_occurrence_index = 0;
        append_heading_results(
            append_heading_results, sections.kids, {}, note, folder_idx, note_idx, folder,
            heading_occurrence_index);
      }
    }

    const auto display_path = [](const NoteSwitcherResult &result) {
      return result.heading_path.empty() ? result.note_title
                                         : result.note_title + "." + result.heading_path;
    };
    std::stable_sort(note_switcher.results.begin(), note_switcher.results.end(),
                     [&](const NoteSwitcherResult &left, const NoteSwitcherResult &right) {
                       if(left.score != right.score) return left.score > right.score;
                       const bool left_is_note = left.heading_path.empty();
                       const bool right_is_note = right.heading_path.empty();
                       if(left_is_note != right_is_note) return left_is_note;
                       const std::string left_path = display_path(left);
                       const std::string right_path = display_path(right);
                       if(left_path.size() != right_path.size()) return left_path.size() < right_path.size();
                       if(left_path != right_path) return left_path < right_path;
                       if(left.note_path != right.note_path) return left.note_path < right.note_path;
                       return left.heading_occurrence_index < right.heading_occurrence_index;
                     });
    if(note_switcher.results.size() > kMaxResults)
      note_switcher.results.resize(kMaxResults);

    if(note_switcher.results.empty())
    {
      note_switcher_selected_idx_ = -1;
      return;
    }
    note_switcher_selected_idx_ = clamp_to_range(
        previous_selected_idx, 0, static_cast<int>(note_switcher.results.size()) - 1);
    if(!selected_note_id.empty())
    {
      for(std::size_t i = 0; i < note_switcher.results.size(); ++i)
      {
        if(note_switcher.results[i].note_id == selected_note_id &&
           note_switcher.results[i].heading_path == selected_heading_path &&
           note_switcher.results[i].heading_occurrence_index == selected_heading_occurrence_index)
        {
          note_switcher_selected_idx_ = static_cast<int>(i);
          break;
        }
      }
    }
  };
  const auto begin_note_header_create = [&](const NoteSwitcherResult &result) {
    note_switcher.query[0] = '\0';
    note_switcher.last_query.clear();
    command_finder.query[0] = '\0';
    command_finder_feedback_.clear();
    note_header_create_note_reference_ = result.note_id.empty() ? result.note_path : result.note_id;
    // The API's parent argument is the exact header title. Keep the switcher's
    // path and occurrence alongside it so selection context is not flattened.
    note_header_create_parent_path_ = result.heading_path;
    note_header_create_parent_occurrence_ = result.heading_occurrence_index;
    note_header_create_parent_ = result.heading_title;
    note_header_create_title_[0] = '\0';
    note_header_create_focus_input_ = true;
    note_header_create_selecting_ = false;
    note_header_create_name_visible_ = true;
    command_finder_navigation_delta_ = 0;
    command_finder_feedback_.clear();
  };
  const auto begin_note_color_set = [&](const NoteSwitcherResult &result) {
    note_switcher.query[0] = '\0';
    note_switcher.last_query.clear();
    command_finder.query[0] = '\0';
    command_finder_feedback_.clear();
    note_color_set_note_reference_ = result.note_id.empty() ? result.note_path : result.note_id;
    note_color_set_color_[0] = 0.35f;
    note_color_set_color_[1] = 0.65f;
    note_color_set_color_[2] = 1.0f;
    note_color_set_selected_preset_ = 3;
    note_color_set_navigation_delta_ = 0;
    note_color_set_focus_input_ = true;
    note_color_set_selecting_ = false;
    note_color_set_visible_ = true;
    command_finder_navigation_delta_ = 0;
    command_finder_feedback_.clear();
  };
  auto open_note_switcher = [&]() {
    if(!request_open_note_switcher_) return;
    note_switcher.visible = true;
    note_switcher.focus_input = true;
    note_switcher.just_opened = true;
    note_switcher.query[0] = '\0';
    note_switcher.last_query.clear();
    note_switcher.results.clear();
    note_switcher_selected_idx_ = -1;
    note_switcher_navigation_delta_ = 0;
    request_note_switcher_activate_ = false;
    search_dialog.visible = false;
    search_selected_idx_ = -1;
    search_navigation_delta_ = 0;
    request_search_activate_ = false;
    search_window_visible_ = false;
    request_open_note_switcher_ = false;
  };
  auto render_note_switcher = [&]() {
    open_note_switcher();
    if(request_close_note_switcher_)
    {
      note_switcher.visible = false;
      if(note_header_create_selecting_)
        note_header_create_selecting_ = false;
      if(note_color_set_selecting_)
      {
        note_color_set_selecting_ = false;
        note_color_set_note_reference_.clear();
      }
      request_close_note_switcher_ = false;
      request_note_switcher_activate_ = false;
      note_switcher_navigation_delta_ = 0;
    }
    note_switcher_window_visible_ = note_switcher.visible;
    if(!note_switcher.visible) return;

    const bool just_opened = note_switcher.just_opened;
    const ImGuiCond placement_cond = just_opened ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), placement_cond);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), placement_cond,
                            ImVec2(0.5f, 0.5f));
    if(just_opened) ImGui::SetNextWindowFocus();
    const char *note_switcher_title = (note_header_create_selecting_ || note_color_set_selecting_)
                                           ? "Command Finder"
                                           : "Quick Note Switcher";
    if(!ImGui::Begin(note_switcher_title, &note_switcher.visible,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoSavedSettings))
    {
      note_switcher.just_opened = false;
      ImGui::End();
      if(!note_switcher.visible && note_header_create_selecting_)
        note_header_create_selecting_ = false;
      if(!note_switcher.visible && note_color_set_selecting_)
      {
        note_color_set_selecting_ = false;
        note_color_set_note_reference_.clear();
      }
      return;
    }
    note_switcher.just_opened = false;

    if(note_switcher.focus_input)
    {
      ImGui::SetKeyboardFocusHere();
      note_switcher.focus_input = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    const bool query_changed = ImGui::InputText(
        "Note name", note_switcher.query, sizeof(note_switcher.query),
        ImGuiInputTextFlags_AutoSelectAll);
    const std::string current_query(note_switcher.query);
    if(query_changed || current_query != note_switcher.last_query || just_opened)
    {
      refresh_note_switcher_results();
      note_switcher.last_query = current_query;
    }

    bool note_switcher_selection_moved = false;
    if(!note_switcher.results.empty() && note_switcher_navigation_delta_ != 0)
    {
      const int previous_selected_idx = note_switcher_selected_idx_;
      note_switcher_selected_idx_ = clamp_to_range(
          note_switcher_selected_idx_ + note_switcher_navigation_delta_, 0,
          static_cast<int>(note_switcher.results.size()) - 1);
      note_switcher_selection_moved = note_switcher_selected_idx_ != previous_selected_idx;
    }
    note_switcher_navigation_delta_ = 0;

    if(request_note_switcher_activate_)
    {
      request_note_switcher_activate_ = false;
      if(note_switcher_selected_idx_ >= 0 &&
         note_switcher_selected_idx_ < static_cast<int>(note_switcher.results.size()))
      {
        const NoteSwitcherResult &result =
            note_switcher.results[static_cast<std::size_t>(note_switcher_selected_idx_)];
        if(note_header_create_selecting_)
          begin_note_header_create(result);
        else if(note_color_set_selecting_)
          begin_note_color_set(result);
        else
          navigate_to_note_switcher_result(result);
        note_switcher.visible = false;
      }
    }

    ImGui::TextDisabled("%zu result%s", note_switcher.results.size(),
                        note_switcher.results.size() == 1 ? "" : "s");
    ImGui::Separator();
    if(ImGui::BeginChild("##note_switcher_results", ImVec2(0, 340.0f), true))
    {
      if(note_switcher.results.empty())
      {
        ImGui::TextDisabled("No matching results.");
      }
      else
      {
        for(size_t i = 0; i < note_switcher.results.size(); ++i)
        {
          const NoteSwitcherResult &result = note_switcher.results[i];
          const std::string display_name = result.heading_path.empty()
                                               ? result.note_title
                                               : result.note_title + "." + result.heading_path;
          const std::string label = display_name + "  [" + result.folder_name + "]##note_switcher_" +
                                    std::to_string(i);
          const bool selected = static_cast<int>(i) == note_switcher_selected_idx_;
          if(ImGui::Selectable(label.c_str(), selected))
          {
            note_switcher_selected_idx_ = static_cast<int>(i);
            if(note_header_create_selecting_)
              begin_note_header_create(result);
            else if(note_color_set_selecting_)
              begin_note_color_set(result);
            else
              navigate_to_note_switcher_result(result);
            note_switcher.visible = false;
          }
          if(selected)
          {
            ImGui::SetItemDefaultFocus();
            if(note_switcher_selection_moved) ImGui::SetScrollHereY(0.5f);
          }
          if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", result.note_path.c_str());
        }
      }
      ImGui::EndChild();
    }
    if(ImGui::Button("Close")) note_switcher.visible = false;
    ImGui::End();
    if(!note_switcher.visible && note_header_create_selecting_)
      note_header_create_selecting_ = false;
    if(!note_switcher.visible && note_color_set_selecting_)
    {
      note_color_set_selecting_ = false;
      note_color_set_note_reference_.clear();
    }
    note_switcher_window_visible_ = note_switcher.visible;
  };
  auto render_terminal = [&]() {
    const bool terminal_opened_this_frame = request_open_terminal_ && !terminal_visible_;
    if(request_open_terminal_)
    {
      terminal_visible_ = !terminal_visible_;
      request_open_terminal_ = false;
    }
    if(!terminal_visible_)
    {
      terminal_.releaseFocus();
      pending_terminal_text_.clear();
      return;
    }

    terminal_.setFont(font_terminal_);
    terminal_.setDefaultWorkingDirectory(config_.dataPath);
    terminal_.setWindowHeight(terminal_height_);
    if(terminal_opened_this_frame && terminal_.sessionCount() == 0)
      terminal_.start(config_.dataPath, 24, 80);
    if(terminal_new_tab_on_open_ && terminal_visible_)
    {
      terminal_.setDefaultWorkingDirectory(config_.dataPath);
      (void)terminal_.addSession(config_.dataPath);
      terminal_new_tab_on_open_ = false;
    }
    // render() resolves current-frame focus after all note/editor widgets,
    // then writes composed text before Enter/navigation events. This avoids
    // stale focus stealing note input and preserves command ordering.
    terminal_.render(&terminal_visible_, pending_terminal_text_, !terminal_opened_this_frame);
    const float rendered_terminal_height = terminal_.windowHeight();
    if(std::fabs(rendered_terminal_height - terminal_height_) > 0.5F)
    {
      terminal_height_ = rendered_terminal_height;
      state_dirty_ = true;
    }
    if(!terminal_visible_) terminal_.releaseFocus();
    pending_terminal_text_.clear();
  };
  auto render_command_finder = [&]() {
    struct ApiParameterForm
    {
      std::string command;
      char note_reference[512] = {};
      char folder[256] = "General";
      char title[256] = {};
      char parent[256] = {};
      char content[4096] = {};
      char variable_name[256] = {};
      char variable_value[2048] = {};
      char raw_json[4096] = "{}";
      int level = 1;
      float color[3] = {0.35f, 0.65f, 1.0f};
      bool raw_json_enabled = false;

      void reset(std::string name)
      {
        command = std::move(name);
        std::strncpy(folder, "General", sizeof(folder));
        folder[sizeof(folder) - 1U] = '\0';
        note_reference[0] = '\0';
        title[0] = '\0';
        parent[0] = '\0';
        content[0] = '\0';
        variable_name[0] = '\0';
        variable_value[0] = '\0';
        std::strncpy(raw_json, "{}", sizeof(raw_json));
        raw_json[sizeof(raw_json) - 1U] = '\0';
        level = 1;
        color[0] = 0.35f; color[1] = 0.65f; color[2] = 1.0f;
        raw_json_enabled = false;
      }
    };
    static ApiParameterForm api_form;
    static const auto is_mutating_command = [](std::string_view command) {
      return command == "note.create" || command == "note.header.create" ||
             command == "note.color.set" || command == "note.variable.set";
    };
    static const auto has_parameter_form = [](std::string_view command) {
      return command == "note.get" || command == "note.create" ||
             command == "note.header.list" || command == "note.header.get" ||
             command == "note.header.create" || command == "note.color.set" ||
             command == "note.variable.get" || command == "note.variable.set";
    };
    const auto write_terminal_response = [&](const nlohmann::json &request,
                                              const notepp::command_api::Response &response) {
      terminal_visible_ = true;
      request_open_terminal_ = false;
      terminal_.setDefaultWorkingDirectory(config_.dataPath);
      if(terminal_.sessionCount() == 0) terminal_.start(config_.dataPath, 24, 80);
      const auto quote_for_shell = [](std::string_view text) {
        std::string quoted = "'";
        for(const char c : text)
        {
          if(c == '\'') quoted += "'\\''";
          else quoted.push_back(c);
        }
        quoted.push_back('\'');
        return quoted;
      };
      terminal_.write("printf '%s\\n' " + quote_for_shell(
                          "$ notepp-cli execute " + request.dump()) + "\n");
      terminal_.write("printf '%s\\n' " + quote_for_shell(response.serialize()) + "\n");
    };
    const auto note_reference_args = [this](const char *reference) {
      nlohmann::json args = nlohmann::json::object();
      const std::string value(reference);
      for(const auto &folder : folders_)
        for(const auto &note : folder.notes)
          if(note.id == value)
          {
            args["id"] = value;
            return args;
          }
      if(value.find('/') != std::string::npos || value.find('\\') != std::string::npos ||
         (value.size() > 3U && value.substr(value.size() - 3U) == ".md"))
        args["path"] = value;
      else if(!value.empty())
        args["name"] = value;
      return args;
    };
    if(note_color_set_visible_ && !command_finder.visible)
    {
      const auto close_guided_note_color_set = [&]() {
        note_color_set_selecting_ = false;
        note_color_set_visible_ = false;
        request_note_color_set_confirm_ = false;
        request_close_note_color_set_ = false;
        note_color_set_focus_input_ = false;
        note_color_set_note_reference_.clear();
        note_color_set_color_[0] = 0.35f;
        note_color_set_color_[1] = 0.65f;
        note_color_set_color_[2] = 1.0f;
        note_color_set_selected_preset_ = 3;
        note_color_set_navigation_delta_ = 0;
      };
      if(request_close_note_color_set_)
        close_guided_note_color_set();
      else
      {
        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(500.0f, 220.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if(note_color_set_focus_input_) ImGui::SetNextWindowFocus();
        if(ImGui::Begin("Command Finder", &open,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                            ImGuiWindowFlags_NoSavedSettings))
        {
          ImGui::TextUnformatted("Choose a color for the selected note");
          if(note_color_set_focus_input_)
          {
            ImGui::SetKeyboardFocusHere();
            note_color_set_focus_input_ = false;
          }
          ImGui::ColorEdit3("Color", note_color_set_color_, ImGuiColorEditFlags_NoInputs);
          static constexpr ImVec4 choices[] = {
              ImVec4(0.95f, 0.35f, 0.35f, 1.0f), ImVec4(0.98f, 0.72f, 0.25f, 1.0f),
              ImVec4(0.35f, 0.75f, 0.45f, 1.0f), ImVec4(0.35f, 0.65f, 1.0f, 1.0f),
              ImVec4(0.70f, 0.45f, 0.90f, 1.0f)};
          if(note_color_set_navigation_delta_ != 0)
          {
            note_color_set_selected_preset_ = clamp_to_range(
                note_color_set_selected_preset_ + note_color_set_navigation_delta_, 0,
                static_cast<int>(std::size(choices)) - 1);
            note_color_set_navigation_delta_ = 0;
            const ImVec4 &choice = choices[static_cast<std::size_t>(note_color_set_selected_preset_)];
            note_color_set_color_[0] = choice.x;
            note_color_set_color_[1] = choice.y;
            note_color_set_color_[2] = choice.z;
          }
          for(std::size_t i = 0; i < std::size(choices); ++i)
          {
            if(i != 0U) ImGui::SameLine();
            const bool selected = static_cast<int>(i) == note_color_set_selected_preset_;
            if(selected)
            {
              ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
              ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            const bool clicked = ImGui::ColorButton(
                ("##guided_note_color_" + std::to_string(i)).c_str(), choices[i]);
            if(selected)
            {
              ImGui::PopStyleColor();
              ImGui::PopStyleVar();
            }
            if(clicked)
            {
              note_color_set_selected_preset_ = static_cast<int>(i);
              note_color_set_color_[0] = choices[i].x;
              note_color_set_color_[1] = choices[i].y;
              note_color_set_color_[2] = choices[i].z;
            }
          }
          if(!command_finder_feedback_.empty())
            ImGui::TextWrapped("Feedback: %s", command_finder_feedback_.c_str());
          if(ImGui::Button("Apply")) request_note_color_set_confirm_ = true;
          ImGui::SameLine();
          if(ImGui::Button("Cancel")) request_close_note_color_set_ = true;
        }
        ImGui::End();
        if(!open) request_close_note_color_set_ = true;

        if(request_note_color_set_confirm_)
        {
          request_note_color_set_confirm_ = false;
          if(command_api_ == nullptr)
            command_finder_feedback_ = "Command API is unavailable";
          else
          {
            nlohmann::json args = note_reference_args(note_color_set_note_reference_.c_str());
            args["r"] = static_cast<int>(std::lround(note_color_set_color_[0] * 255.0f));
            args["g"] = static_cast<int>(std::lround(note_color_set_color_[1] * 255.0f));
            args["b"] = static_cast<int>(std::lround(note_color_set_color_[2] * 255.0f));
            const nlohmann::json request{{"command", "note.color.set"}, {"args", args}};
            const auto response = command_api_->execute(request);
            command_finder_feedback_ = response.body.dump();
            if(response.ok)
            {
              post_command_mutation(request, response);
              close_guided_note_color_set();
            }
            else
              LOG_WARNING("Command failed: ", response.body.dump());
          }
        }
        if(request_close_note_color_set_)
          close_guided_note_color_set();
      }
      return;
    }
    if(note_header_create_name_visible_ && !command_finder.visible)
    {
      const auto close_guided_header_create = [&]() {
        note_header_create_selecting_ = false;
        note_header_create_name_visible_ = false;
        request_note_header_create_confirm_ = false;
        request_close_note_header_create_ = false;
        note_header_create_focus_input_ = false;
        note_header_create_title_[0] = '\0';
        note_header_create_note_reference_.clear();
        note_header_create_parent_.clear();
        note_header_create_parent_path_.clear();
        note_header_create_parent_occurrence_ = 0;
      };
      if(request_close_note_header_create_)
        close_guided_header_create();
      else
      {
        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(500.0f, 220.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if(note_header_create_focus_input_) ImGui::SetNextWindowFocus();
        if(ImGui::Begin("Command Finder", &open,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                            ImGuiWindowFlags_NoSavedSettings))
        {
          ImGui::TextUnformatted("New header");
          if(!note_header_create_parent_.empty())
            ImGui::TextDisabled("Under: %s", note_header_create_parent_path_.c_str());
          else
            ImGui::TextDisabled("At the end of the selected note");
          if(note_header_create_focus_input_)
          {
            ImGui::SetKeyboardFocusHere();
            note_header_create_focus_input_ = false;
          }
          ImGui::SetNextItemWidth(-1.0f);
          ImGui::InputText("Name", note_header_create_title_,
                           sizeof(note_header_create_title_), ImGuiInputTextFlags_AutoSelectAll);
          if(!command_finder_feedback_.empty())
            ImGui::TextWrapped("Feedback: %s", command_finder_feedback_.c_str());
          if(ImGui::Button("Create")) request_note_header_create_confirm_ = true;
          ImGui::SameLine();
          if(ImGui::Button("Cancel")) request_close_note_header_create_ = true;
        }
        ImGui::End();
        if(!open) request_close_note_header_create_ = true;

        if(request_note_header_create_confirm_)
        {
          request_note_header_create_confirm_ = false;
          if(note_header_create_title_[0] == '\0')
            command_finder_feedback_ = "Invalid parameters: title is required";
          else if(command_api_ == nullptr)
            command_finder_feedback_ = "Command API is unavailable";
          else
          {
            nlohmann::json args = note_reference_args(note_header_create_note_reference_.c_str());
            args["title"] = note_header_create_title_;
            args["parent"] = note_header_create_parent_;
            args["level"] = 1;
            const nlohmann::json request{{"command", "note.header.create"}, {"args", args}};
            const auto response = command_api_->execute(request);
            command_finder_feedback_ = response.body.dump();
            if(response.ok)
            {
              post_command_mutation(request, response);
              close_guided_header_create();
            }
            else
              LOG_WARNING("Command failed: ", response.body.dump());
          }
        }
        if(request_close_note_header_create_)
          close_guided_header_create();
      }
      return;
    }
    if(!api_form.command.empty())
    {
      bool open = true;
      ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_Appearing);
      if(ImGui::Begin("Command Parameters", &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::TextUnformatted(api_form.command.c_str());
        nlohmann::json args = nlohmann::json::object();
        const bool known_form = has_parameter_form(api_form.command);
        if(known_form && api_form.command != "note.create" && api_form.command != "app.capabilities" &&
           api_form.command != "note.list")
        {
          ImGui::SetNextItemWidth(-1.0f);
          ImGui::InputText("Note ID, path, or name", api_form.note_reference,
                           sizeof(api_form.note_reference));
        }
        if(api_form.command == "note.create")
        {
          ImGui::InputText("Folder", api_form.folder, sizeof(api_form.folder));
          ImGui::InputText("Title", api_form.title, sizeof(api_form.title));
          ImGui::InputTextMultiline("Content", api_form.content, sizeof(api_form.content), ImVec2(-1.0f, 130.0f));
        }
        else if(api_form.command == "note.header.create")
        {
          ImGui::InputText("Title", api_form.title, sizeof(api_form.title));
          ImGui::InputText("Parent (optional)", api_form.parent, sizeof(api_form.parent));
          ImGui::SliderInt("Level", &api_form.level, 1, 6);
        }
        else if(api_form.command == "note.header.get")
          ImGui::InputText("Header title", api_form.title, sizeof(api_form.title));
        else if(api_form.command == "note.color.set")
        {
          ImGui::TextDisabled("Choose a note color");
          ImGui::ColorEdit3("Color", api_form.color, ImGuiColorEditFlags_NoInputs);
          static constexpr ImVec4 choices[] = {
              ImVec4(0.95f, 0.35f, 0.35f, 1.0f), ImVec4(0.98f, 0.72f, 0.25f, 1.0f),
              ImVec4(0.35f, 0.75f, 0.45f, 1.0f), ImVec4(0.35f, 0.65f, 1.0f, 1.0f),
              ImVec4(0.70f, 0.45f, 0.90f, 1.0f)};
          for(std::size_t i = 0; i < std::size(choices); ++i)
          {
            if(i != 0U) ImGui::SameLine();
            if(ImGui::ColorButton(("##note_color_" + std::to_string(i)).c_str(), choices[i]))
            {
              api_form.color[0] = choices[i].x; api_form.color[1] = choices[i].y; api_form.color[2] = choices[i].z;
            }
          }
        }
        else if(api_form.command == "note.variable.get" || api_form.command == "note.variable.set")
        {
          ImGui::InputText("Variable name", api_form.variable_name, sizeof(api_form.variable_name));
          if(api_form.command == "note.variable.set")
            ImGui::InputTextMultiline("Value", api_form.variable_value, sizeof(api_form.variable_value), ImVec2(-1.0f, 100.0f));
        }
        if(!known_form)
        {
          ImGui::Separator();
          ImGui::TextDisabled("This command has no registered form.");
          ImGui::Checkbox("Advanced: raw JSON arguments", &api_form.raw_json_enabled);
          if(api_form.raw_json_enabled)
            ImGui::InputTextMultiline("##advanced_command_parameters", api_form.raw_json,
                                      sizeof(api_form.raw_json), ImVec2(-1.0f, 120.0f));
        }
        if(!command_finder_feedback_.empty())
          ImGui::TextWrapped("Feedback: %s", command_finder_feedback_.c_str());
        if(ImGui::Button("Run"))
        {
          bool valid = true;
          if(!api_form.raw_json_enabled)
          {
            const auto require = [&](const char *value, const char *field) {
              if(value[0] != '\0') return true;
              command_finder_feedback_ = std::string("Invalid parameters: ") + field + " is required";
              return false;
            };
            const bool needs_reference = api_form.command == "note.get" ||
                                         api_form.command == "note.header.list" ||
                                         api_form.command == "note.header.get" ||
                                         api_form.command == "note.header.create" ||
                                         api_form.command == "note.color.set" ||
                                         api_form.command == "note.variable.get" ||
                                         api_form.command == "note.variable.set";
            if(needs_reference) valid = require(api_form.note_reference, "note reference") && valid;
            if(api_form.command == "note.create")
            {
              valid = require(api_form.folder, "folder") && valid;
              valid = require(api_form.title, "title") && valid;
            }
            else if(api_form.command == "note.header.create" || api_form.command == "note.header.get")
              valid = require(api_form.title, "title") && valid;
            if(api_form.command == "note.variable.get" || api_form.command == "note.variable.set")
              valid = require(api_form.variable_name, "variable name") && valid;
          }
          try
          {
            if(valid && api_form.raw_json_enabled)
            {
              args = nlohmann::json::parse(api_form.raw_json);
              valid = args.is_object();
              if(!valid) command_finder_feedback_ = "Invalid parameters: arguments must be an object";
            }
            else if(valid && api_form.command == "note.create")
            {
              args = {{"folder", api_form.folder}, {"title", api_form.title}, {"content", api_form.content}};
            }
            else if(valid && api_form.command == "note.header.create")
            {
              args = note_reference_args(api_form.note_reference);
              args["title"] = api_form.title; args["parent"] = api_form.parent; args["level"] = api_form.level;
            }
            else if(valid && api_form.command == "note.header.get")
            {
              args = note_reference_args(api_form.note_reference); args["title"] = api_form.title;
            }
            else if(valid && api_form.command == "note.color.set")
            {
              args = note_reference_args(api_form.note_reference);
              args["r"] = static_cast<int>(std::lround(api_form.color[0] * 255.0f));
              args["g"] = static_cast<int>(std::lround(api_form.color[1] * 255.0f));
              args["b"] = static_cast<int>(std::lround(api_form.color[2] * 255.0f));
            }
            else if(valid && (api_form.command == "note.variable.get" || api_form.command == "note.variable.set"))
            {
              args = note_reference_args(api_form.note_reference); args["name"] = api_form.variable_name;
              if(api_form.command == "note.variable.set")
              {
                const auto value = nlohmann::json::parse(api_form.variable_value, nullptr, false);
                args["value"] = value.is_discarded() ? nlohmann::json(api_form.variable_value) : value;
              }
            }
            else if(valid && (api_form.command == "note.get" || api_form.command == "note.header.list"))
              args = note_reference_args(api_form.note_reference);
            else if(valid && api_form.command == "note.list")
              args = nlohmann::json::object();
            else if(valid && api_form.command == "app.capabilities")
              args = nlohmann::json::object();
          }
          catch(const std::exception &error)
          {
            valid = false;
            command_finder_feedback_ = std::string("Invalid parameters: ") + error.what();
          }
          if(valid)
          {
            const nlohmann::json request{{"command", api_form.command}, {"args", args}};
            const auto response = command_api_->execute(request);
            if(is_mutating_command(api_form.command))
            {
              command_finder_feedback_ = response.body.dump();
              if(response.ok) post_command_mutation(request, response);
              else LOG_WARNING("Command failed: ", response.body.dump());
            }
            else
            {
              command_finder_feedback_.clear();
              write_terminal_response(request, response);
            }
            if(response.ok || !is_mutating_command(api_form.command)) api_form.command.clear();
          }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) api_form.command.clear();
      }
      ImGui::End();
      if(!open) api_form.command.clear();
    }
    if(request_open_command_finder_)
    {
      command_finder.visible = true;
      command_finder.focus_input = true;
      command_finder.query[0] = '\0';
      command_finder.selected = 0;
      command_finder_navigation_delta_ = 0;
      request_open_command_finder_ = false;
    }
    if(request_close_command_finder_)
    {
      command_finder.visible = false;
      request_close_command_finder_ = false;
      request_activate_command_finder_ = false;
      command_finder_navigation_delta_ = 0;
      note_header_create_selecting_ = false;
      note_header_create_name_visible_ = false;
      request_note_header_create_confirm_ = false;
      request_close_note_header_create_ = false;
      note_header_create_focus_input_ = false;
      note_header_create_title_[0] = '\0';
      note_color_set_selecting_ = false;
      note_color_set_visible_ = false;
      request_note_color_set_confirm_ = false;
      request_close_note_color_set_ = false;
      note_color_set_focus_input_ = false;
      note_color_set_selected_preset_ = 3;
      note_color_set_navigation_delta_ = 0;
      note_header_create_note_reference_.clear();
      note_header_create_parent_.clear();
      note_header_create_parent_path_.clear();
      note_color_set_note_reference_.clear();
    }
    command_finder_window_visible_ = command_finder.visible;
    if(!command_finder.visible) return;

    // Guided note commands deliberately stay in the command finder window.  The
    // note switcher's result generation remains the source for this list, but
    // its widgets are rendered here so selecting a note never opens a second
    // window or changes the ImGui window identity.
    const bool guided_command = note_header_create_selecting_ ||
                                note_color_set_selecting_ ||
                                note_header_create_name_visible_ ||
                                note_color_set_visible_;
    if(guided_command)
    {
      const auto close_guided_command = [&]() {
        note_header_create_selecting_ = false;
        note_header_create_name_visible_ = false;
        note_header_create_focus_input_ = false;
        note_header_create_title_[0] = '\0';
        note_header_create_note_reference_.clear();
        note_header_create_parent_.clear();
        note_header_create_parent_path_.clear();
        note_header_create_parent_occurrence_ = 0;
        request_note_header_create_confirm_ = false;
        note_color_set_selecting_ = false;
        note_color_set_visible_ = false;
        note_color_set_focus_input_ = false;
        note_color_set_note_reference_.clear();
        note_color_set_selected_preset_ = 3;
        note_color_set_navigation_delta_ = 0;
        request_note_color_set_confirm_ = false;
        command_finder.visible = false;
        command_finder_feedback_.clear();
      };
      if(note_header_create_selecting_ || note_color_set_selecting_)
      {
        if(request_activate_command_finder_ && note_switcher_selected_idx_ >= 0 &&
           note_switcher_selected_idx_ < static_cast<int>(note_switcher.results.size()))
        {
          const NoteSwitcherResult &result = note_switcher.results[
              static_cast<std::size_t>(note_switcher_selected_idx_)];
          request_activate_command_finder_ = false;
          if(note_header_create_selecting_) begin_note_header_create(result);
          else begin_note_color_set(result);
        }

        if(command_finder_navigation_delta_ != 0 && !note_switcher.results.empty())
        {
          note_switcher_selected_idx_ = clamp_to_range(
              note_switcher_selected_idx_ + command_finder_navigation_delta_, 0,
              static_cast<int>(note_switcher.results.size()) - 1);
          command_finder_navigation_delta_ = 0;
        }
      }

      ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_Appearing);
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                              ImVec2(0.5f, 0.5f));
      if(!ImGui::Begin("Command Finder", &command_finder.visible,
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::End();
        if(!command_finder.visible) close_guided_command();
        command_finder_window_visible_ = command_finder.visible;
        return;
      }

      if(note_header_create_selecting_ || note_color_set_selecting_)
      {
        if(command_finder.focus_input)
        {
          ImGui::SetKeyboardFocusHere();
          command_finder.focus_input = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if(ImGui::InputTextWithHint("##guided_command_filter", "Search notes or headers...",
                                   note_switcher.query, sizeof(note_switcher.query)))
        {
          note_switcher_selected_idx_ = 0;
          note_switcher_navigation_delta_ = 0;
        }
        const std::string current_query(note_switcher.query);
        if(current_query != note_switcher.last_query || note_switcher.just_opened)
        {
          refresh_note_switcher_results();
          note_switcher.last_query = current_query;
          note_switcher.just_opened = false;
        }
        ImGui::TextDisabled("%zu result%s", note_switcher.results.size(),
                            note_switcher.results.size() == 1 ? "" : "s");
        ImGui::Separator();
        if(ImGui::BeginChild("##command_guided_results", ImVec2(0.0f, 340.0f), true))
        {
          if(note_switcher.results.empty()) ImGui::TextDisabled("No matching results.");
          for(std::size_t i = 0; i < note_switcher.results.size(); ++i)
          {
            const NoteSwitcherResult &result = note_switcher.results[i];
            const std::string display_name = result.heading_path.empty()
                                                 ? result.note_title
                                                 : result.note_title + "." + result.heading_path;
            const std::string label = display_name + "  [" + result.folder_name + "]##command_guided_" +
                                      std::to_string(i);
            const bool selected = static_cast<int>(i) == note_switcher_selected_idx_;
            if(ImGui::Selectable(label.c_str(), selected))
            {
              note_switcher_selected_idx_ = static_cast<int>(i);
              request_activate_command_finder_ = true;
            }
            if(selected) ImGui::SetItemDefaultFocus();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", result.note_path.c_str());
          }
          ImGui::EndChild();
        }
      }
      else if(note_header_create_name_visible_)
      {
        ImGui::TextUnformatted("New header");
        if(!note_header_create_parent_.empty())
          ImGui::TextDisabled("Under: %s", note_header_create_parent_path_.c_str());
        else
          ImGui::TextDisabled("At the end of the selected note");
        if(note_header_create_focus_input_)
        {
          ImGui::SetKeyboardFocusHere();
          note_header_create_focus_input_ = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Name", note_header_create_title_, sizeof(note_header_create_title_),
                         ImGuiInputTextFlags_AutoSelectAll);
        if(!command_finder_feedback_.empty())
          ImGui::TextWrapped("Feedback: %s", command_finder_feedback_.c_str());
        if(ImGui::Button("Create")) request_note_header_create_confirm_ = true;
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) request_close_command_finder_ = true;
        if(request_activate_command_finder_)
        {
          request_activate_command_finder_ = false;
          request_note_header_create_confirm_ = true;
        }
      }
      else
      {
        ImGui::TextUnformatted("Choose a color for the selected note");
        if(note_color_set_focus_input_)
        {
          ImGui::SetKeyboardFocusHere();
          note_color_set_focus_input_ = false;
        }
        ImGui::ColorEdit3("Color", note_color_set_color_, ImGuiColorEditFlags_NoInputs);
        static constexpr ImVec4 choices[] = {
            ImVec4(0.95f, 0.35f, 0.35f, 1.0f), ImVec4(0.98f, 0.72f, 0.25f, 1.0f),
            ImVec4(0.35f, 0.75f, 0.45f, 1.0f), ImVec4(0.35f, 0.65f, 1.0f, 1.0f),
            ImVec4(0.70f, 0.45f, 0.90f, 1.0f)};
        if(note_color_set_navigation_delta_ != 0)
        {
          note_color_set_selected_preset_ = clamp_to_range(
              note_color_set_selected_preset_ + note_color_set_navigation_delta_, 0,
              static_cast<int>(std::size(choices)) - 1);
          note_color_set_navigation_delta_ = 0;
          const ImVec4 &choice = choices[static_cast<std::size_t>(note_color_set_selected_preset_)];
          note_color_set_color_[0] = choice.x;
          note_color_set_color_[1] = choice.y;
          note_color_set_color_[2] = choice.z;
        }
        for(std::size_t i = 0; i < std::size(choices); ++i)
        {
          if(i != 0U) ImGui::SameLine();
          const bool selected = static_cast<int>(i) == note_color_set_selected_preset_;
          if(selected)
          {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
          }
          const bool clicked = ImGui::ColorButton(
              ("##guided_command_color_" + std::to_string(i)).c_str(), choices[i]);
          if(selected)
          {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
          }
          if(clicked)
          {
            note_color_set_selected_preset_ = static_cast<int>(i);
            note_color_set_color_[0] = choices[i].x;
            note_color_set_color_[1] = choices[i].y;
            note_color_set_color_[2] = choices[i].z;
          }
        }
        if(!command_finder_feedback_.empty())
          ImGui::TextWrapped("Feedback: %s", command_finder_feedback_.c_str());
        if(ImGui::Button("Apply")) request_note_color_set_confirm_ = true;
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) request_close_command_finder_ = true;
        if(request_activate_command_finder_)
        {
          request_activate_command_finder_ = false;
          request_note_color_set_confirm_ = true;
        }
      }
      ImGui::End();

      if(!command_finder.visible || request_close_command_finder_)
        close_guided_command();
      if(request_note_header_create_confirm_)
      {
        request_note_header_create_confirm_ = false;
        if(note_header_create_title_[0] == '\0')
          command_finder_feedback_ = "Invalid parameters: title is required";
        else if(command_api_ == nullptr)
          command_finder_feedback_ = "Command API is unavailable";
        else
        {
          nlohmann::json args = note_reference_args(note_header_create_note_reference_.c_str());
          args["title"] = note_header_create_title_;
          args["parent"] = note_header_create_parent_;
          args["level"] = 1;
          const nlohmann::json request{{"command", "note.header.create"}, {"args", args}};
          const auto response = command_api_->execute(request);
          command_finder_feedback_ = response.body.dump();
          if(response.ok)
          {
            post_command_mutation(request, response);
            close_guided_command();
          }
          else LOG_WARNING("Command failed: ", response.body.dump());
        }
      }
      if(request_note_color_set_confirm_)
      {
        request_note_color_set_confirm_ = false;
        if(command_api_ == nullptr)
          command_finder_feedback_ = "Command API is unavailable";
        else
        {
          nlohmann::json args = note_reference_args(note_color_set_note_reference_.c_str());
          args["r"] = static_cast<int>(std::lround(note_color_set_color_[0] * 255.0f));
          args["g"] = static_cast<int>(std::lround(note_color_set_color_[1] * 255.0f));
          args["b"] = static_cast<int>(std::lround(note_color_set_color_[2] * 255.0f));
          const nlohmann::json request{{"command", "note.color.set"}, {"args", args}};
          const auto response = command_api_->execute(request);
          command_finder_feedback_ = response.body.dump();
          if(response.ok)
          {
            post_command_mutation(request, response);
            close_guided_command();
          }
          else LOG_WARNING("Command failed: ", response.body.dump());
        }
      }
      command_finder_window_visible_ = command_finder.visible;
      return;
    }

    struct Command
    {
      CommandFinderCommand id;
      std::string label;
      std::string api_name;
    };
    std::vector<Command> commands = {
        {CommandFinderCommand::terminal, "Open / Hide Terminal", {}},
        {CommandFinderCommand::focus_active_note, "Focus Active Note", {}},
        {CommandFinderCommand::new_note, "Create New Note", {}},
        {CommandFinderCommand::hide_focused_note, "Hide Focused Note", {}},
        {CommandFinderCommand::hide_folder_notes, "Hide Visible Notes in Active Folder", {}},
        {CommandFinderCommand::quick_open, "Quick Open", {}},
        {CommandFinderCommand::find_current_note, "Find Current Note", {}},
        {CommandFinderCommand::find_in_project, "Find in Project", {}}};
    // Keep the finder usable even while the project command adapter is being
    // initialized. The command names are also the protocol's single source
    // of truth; the registry supplies the descriptors when available.
    static constexpr const char *api_command_names[] = {
        "app.capabilities", "note.list", "note.get", "note.create",
        "note.header.list", "note.header.get", "note.header.create",
        "note.color.set", "note.variable.get", "note.variable.set"};
    for(const char *name : api_command_names)
      commands.push_back({CommandFinderCommand::api_command, name, name});
    if(command_api_ != nullptr)
    {
      // The registry may add future commands without requiring GUI changes.
      for(const auto &entry : command_api_->capabilities().at("commands"))
      {
        const std::string name = entry.value("name", std::string{});
        if(std::find_if(commands.begin(), commands.end(), [&](const Command &command) {
             return command.api_name == name;
           }) == commands.end())
          commands.push_back({CommandFinderCommand::api_command, name, name});
      }
    }
    std::string query_lower = to_lower_ascii(command_finder.query);
    // Accept both the protocol spelling ("note.list") and the natural
    // command-finder spelling ("note list").
    std::replace(query_lower.begin(), query_lower.end(), ' ', '.');
    std::vector<const Command *> matches;
    matches.reserve(commands.size());
    for(const Command &command : commands)
    {
      const std::string label_lower = to_lower_ascii(command.label);
      const std::string api_name_lower = to_lower_ascii(command.api_name);
      if(query_lower.empty() || label_lower.find(query_lower) != std::string::npos ||
         api_name_lower.find(query_lower) != std::string::npos)
        matches.push_back(&command);
    }
    if(matches.empty())
    {
      command_finder.selected = -1;
      command_finder_navigation_delta_ = 0;
    }
    else
      command_finder.selected = std::max(0, std::min(command_finder.selected, static_cast<int>(matches.size()) - 1));
    if(command_finder_navigation_delta_ != 0 && !matches.empty())
    {
      command_finder.selected = std::max(0, std::min(command_finder.selected + command_finder_navigation_delta_, static_cast<int>(matches.size()) - 1));
      command_finder_navigation_delta_ = 0;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(!ImGui::Begin("Command Finder", &command_finder.visible,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
    {
      ImGui::End();
      return;
    }
    if(command_finder.focus_input)
    {
      ImGui::SetKeyboardFocusHere();
      command_finder.focus_input = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if(ImGui::InputTextWithHint("##command_filter", "Search commands...", command_finder.query, sizeof(command_finder.query)))
    {
      command_finder.selected = 0;
      command_finder_navigation_delta_ = 0;
    }
    ImGui::Separator();
    if(!command_finder_feedback_.empty())
      ImGui::TextWrapped("Result: %s", command_finder_feedback_.c_str());
    if(ImGui::BeginChild("##command_results", ImVec2(0.0f, 0.0f), false))
    {
      for(std::size_t i = 0; i < matches.size(); ++i)
      {
        const bool selected = static_cast<int>(i) == command_finder.selected;
        if(ImGui::Selectable(matches[i]->label.c_str(), selected))
        {
          command_finder.selected = static_cast<int>(i);
          request_activate_command_finder_ = true;
        }
        if(selected) ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndChild();
    if(request_activate_command_finder_ && command_finder.selected >= 0 && command_finder.selected < static_cast<int>(matches.size()))
    {
      switch(matches[static_cast<std::size_t>(command_finder.selected)]->id)
      {
      case CommandFinderCommand::terminal:
        request_open_terminal_ = true;
        break;
      case CommandFinderCommand::focus_active_note:
        request_focus_active_note_ = true;
        break;
      case CommandFinderCommand::new_note:
        request_new_note_ = true;
        break;
      case CommandFinderCommand::hide_focused_note:
        request_hide_focused_note_ = true;
        break;
      case CommandFinderCommand::hide_folder_notes:
        request_hide_folder_notes_ = true;
        break;
      case CommandFinderCommand::quick_open:
        request_open_note_switcher_ = true;
        break;
      case CommandFinderCommand::find_current_note:
        request_open_search_ = true;
        break;
      case CommandFinderCommand::find_in_project:
        request_open_project_search_ = true;
        break;
      case CommandFinderCommand::api_command:
        if(command_api_ != nullptr)
        {
          const std::string &api_name = matches[static_cast<std::size_t>(command_finder.selected)]->api_name;
          if(api_name == "note.header.create")
          {
            command_finder.query[0] = '\0';
            api_form.command.clear();
            note_header_create_selecting_ = true;
            note_header_create_name_visible_ = false;
            note_header_create_focus_input_ = false;
            note_switcher.visible = false;
            note_switcher.focus_input = false;
            note_switcher.just_opened = true;
            note_switcher.query[0] = '\0';
            note_switcher.last_query.clear();
            note_switcher.results.clear();
            note_switcher_selected_idx_ = -1;
            note_switcher_navigation_delta_ = 0;
            command_finder.focus_input = true;
            command_finder_navigation_delta_ = 0;
            command_finder_feedback_.clear();
          }
          else if(api_name == "note.color.set")
          {
            command_finder.query[0] = '\0';
            api_form.command.clear();
            note_color_set_selecting_ = true;
            note_color_set_visible_ = false;
            note_color_set_focus_input_ = false;
            note_switcher.visible = false;
            note_switcher.focus_input = false;
            note_switcher.just_opened = true;
            note_switcher.query[0] = '\0';
            note_switcher.last_query.clear();
            note_switcher.results.clear();
            note_switcher_selected_idx_ = -1;
            note_switcher_navigation_delta_ = 0;
            command_finder.focus_input = true;
            command_finder_navigation_delta_ = 0;
            command_finder_feedback_.clear();
          }
          else
            api_form.reset(api_name);
          // Commands without arguments still get a confirmation/result path;
          // information responses are printed in the embedded terminal.
          if(api_name == "app.capabilities" || api_name == "note.list")
          {
            const nlohmann::json request{{"command", api_name}, {"args", nlohmann::json::object()}};
            const auto response = command_api_->execute(request);
            command_finder_feedback_.clear();
            write_terminal_response(request, response);
            api_form.command.clear();
            if(!response.ok) LOG_WARNING("Command failed: ", response.body.dump());
          }
        }
        break;
      }
      const bool guided_command = note_header_create_selecting_ ||
                                  note_color_set_selecting_ ||
                                  note_header_create_name_visible_ ||
                                  note_color_set_visible_;
      if(!guided_command) command_finder.visible = false;
      request_activate_command_finder_ = false;
    }
    ImGui::End();
    command_finder_window_visible_ = command_finder.visible;
  };
  auto render_editor_actions = [&]() {
    const bool just_opened = request_open_editor_actions_;
    if(just_opened)
    {
      request_open_editor_actions_ = false;
      editor_actions_window_visible_ = true;
      editor_actions_navigation_delta_ = 0;
    }
    if(request_close_editor_actions_)
    {
      editor_actions_window_visible_ = false;
      request_close_editor_actions_ = false;
      request_activate_editor_actions_ = false;
    }
    if(!editor_actions_window_visible_) return;

    struct Action
    {
      EditorAction id;
      const char *label;
      bool needs_selection;
    };
    static constexpr Action actions[] = {
        {EditorAction::task_list, "Insert task list", false},
        {EditorAction::table, "Insert table", false},
        {EditorAction::bold, "Bold", true},
        {EditorAction::italic, "Italic", true},
        {EditorAction::strikethrough, "Strikethrough", true},
        {EditorAction::quote, "Insert quote", true},
        {EditorAction::color, "Text color", true},
        {EditorAction::ui_block, "Insert UI block", false}};
    static char query[128] = {};
    static int selected = 0;
    static bool focus_input = false;
    if(just_opened)
    {
      query[0] = '\0';
      selected = 0;
      focus_input = true;
    }
    const std::string query_lower = to_lower_ascii(query);
    std::vector<const Action *> matches;
    matches.reserve(8);
    for(const Action &action : actions)
    {
      if(query_lower.empty() || to_lower_ascii(action.label).find(query_lower) != std::string::npos)
        matches.push_back(&action);
    }
    const auto action_available = [&](std::size_t index) {
      return !matches[index]->needs_selection || editor_action_selection_available_;
    };
    if(matches.empty())
    {
      selected = -1;
      editor_actions_navigation_delta_ = 0;
    }
    else
    {
      selected = std::max(0, std::min(selected, static_cast<int>(matches.size()) - 1));
      if(!action_available(static_cast<std::size_t>(selected)))
      {
        const auto first_available = std::find_if(matches.begin(), matches.end(), [&](const Action *action) {
          return !action->needs_selection || editor_action_selection_available_;
        });
        selected = first_available == matches.end() ? -1 : static_cast<int>(std::distance(matches.begin(), first_available));
      }
    }
    if(editor_actions_navigation_delta_ != 0 && !matches.empty())
    {
      const int direction = editor_actions_navigation_delta_ > 0 ? 1 : -1;
      int steps = std::abs(editor_actions_navigation_delta_);
      if(selected < 0)
      {
        if(direction > 0)
        {
          const auto first_available = std::find_if(matches.begin(), matches.end(), [&](const Action *action) {
            return !action->needs_selection || editor_action_selection_available_;
          });
          selected = first_available == matches.end() ? -1 : static_cast<int>(std::distance(matches.begin(), first_available));
        }
        else
        {
          const auto first_available = std::find_if(matches.rbegin(), matches.rend(), [&](const Action *action) {
            return !action->needs_selection || editor_action_selection_available_;
          });
          selected = first_available == matches.rend() ? -1 : static_cast<int>(std::distance(matches.begin(), first_available.base()) - 1);
        }
        if(selected >= 0) --steps;
      }
      while(steps-- > 0 && selected >= 0)
      {
        int candidate = selected;
        do
        {
          candidate = (candidate + direction + static_cast<int>(matches.size())) % static_cast<int>(matches.size());
        } while(candidate != selected && !action_available(static_cast<std::size_t>(candidate)));
        selected = candidate;
      }
      editor_actions_navigation_delta_ = 0;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 380.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(!ImGui::Begin("Editor Actions", &editor_actions_window_visible_,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
    {
      ImGui::End();
      return;
    }
    if(focus_input)
    {
      ImGui::SetKeyboardFocusHere();
      focus_input = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if(ImGui::InputTextWithHint("##editor_action_filter", "Search editor actions...", query, sizeof(query)))
    {
      selected = 0;
      editor_actions_navigation_delta_ = 0;
    }
    ImGui::Separator();
    for(std::size_t i = 0; i < matches.size(); ++i)
    {
      const bool available = !matches[i]->needs_selection || editor_action_selection_available_;
      if(!available) ImGui::BeginDisabled();
      const bool is_selected = static_cast<int>(i) == selected;
      if(ImGui::Selectable(matches[i]->label, is_selected) && available)
      {
        selected = static_cast<int>(i);
        request_activate_editor_actions_ = true;
      }
      if(is_selected) ImGui::SetItemDefaultFocus();
      if(!available) ImGui::EndDisabled();
    }
    if(request_activate_editor_actions_)
    {
      if(selected >= 0 && selected < static_cast<int>(matches.size()))
      {
        const Action &action = *matches[static_cast<std::size_t>(selected)];
        if(!action.needs_selection || editor_action_selection_available_)
        {
          request_editor_action_ = action.id;
          editor_actions_window_visible_ = false;
        }
      }
      // Consume Enter even when no executable action is selected. Otherwise a
      // later filter change could activate an action without another Enter.
      request_activate_editor_actions_ = false;
    }
    ImGui::End();
  };
  auto queue_pending_delete_path = [&](const std::string &path) {
    if(path.empty()) return;
    if(std::find(pending_fs_delete_paths_.begin(), pending_fs_delete_paths_.end(), path) == pending_fs_delete_paths_.end())
      pending_fs_delete_paths_.push_back(path);
  };
  auto capture_workspace_before = [&]() {
    flush_pending_text_history();
    return capture_workspace_snapshot();
  };
  auto record_workspace_after = [&](std::string_view label, std::string before_snapshot) {
    record_workspace_history_action(label, std::move(before_snapshot));
  };
  auto perform_workspace_change = [&](std::string_view label, auto &&mutate) {
    std::string before_snapshot = capture_workspace_before();
    mutate();
    record_workspace_after(label, std::move(before_snapshot));
  };
  auto push_sidebar_snapshot = [&]() {
    if(deferred_sidebar_snapshot_before.empty()) deferred_sidebar_snapshot_before = capture_workspace_before();
  };
  auto apply_sidebar_undo = [&]() -> bool { return apply_global_undo(); };
  auto apply_sidebar_redo = [&]() -> bool { return apply_global_redo(); };

  auto focus_visible_note = [&](int preferred_idx, int direction) {
    if(active_folder_idx_ < 0 || active_folder_idx_ >= static_cast<int>(folders_.size())) return false;
    const auto &notes = folders_[static_cast<std::size_t>(active_folder_idx_)].notes;
    if(notes.empty()) return false;
    const int count = static_cast<int>(notes.size());
    for(int step = 0; step < count; ++step)
    {
      const int idx = (preferred_idx + direction * step + count * 2) % count;
      if(notes[static_cast<std::size_t>(idx)].hidden) continue;
      if(editing_mode_ && idx != active_note_idx_)
      {
        normalize_input_text_buffer(markdown_text_);
        state_dirty_ = true;
        editing_mode_ = false;
      }
      active_note_idx_ = idx;
      selected_note_indices.clear();
      selected_note_indices.insert(idx);
      selected_stroke_indices.clear();
      folder_overview_mode_ = true;
      load_note_content_for_active();
      pending_focus_note_idx = idx;
      save_index();
      return true;
    }
    return false;
  };

  if(request_new_note_)
  {
    open_new_note_popup = true;
    new_note_target_folder_idx = active_folder_idx_;
    request_new_note_ = false;
  }
  if(request_hide_focused_note_)
  {
    if(has_active_note())
    {
      const int hidden_idx = active_note_idx_;
      push_sidebar_snapshot();
      FolderMeta &folder = folders_[static_cast<std::size_t>(active_folder_idx_)];
      folder.notes[static_cast<std::size_t>(hidden_idx)].hidden = true;
      if(editing_mode_)
      {
        normalize_input_text_buffer(markdown_text_);
        state_dirty_ = true;
        editing_mode_ = false;
      }
      if(!focus_visible_note((hidden_idx + 1) % std::max(1, static_cast<int>(folder.notes.size())), 1))
      {
        active_note_idx_ = -1;
        selected_note_indices.clear();
        folder_overview_mode_ = true;
        load_note_content_for_active();
        request_focus_sidebar_ = true;
        save_index();
      }
    }
    request_hide_focused_note_ = false;
  }
  if(request_hide_folder_notes_)
  {
    if(active_folder_idx_ >= 0 && active_folder_idx_ < static_cast<int>(folders_.size()))
    {
      push_sidebar_snapshot();
      FolderMeta &folder = folders_[static_cast<std::size_t>(active_folder_idx_)];
      for(NoteMeta &note : folder.notes) note.hidden = true;
      if(editing_mode_)
      {
        normalize_input_text_buffer(markdown_text_);
        state_dirty_ = true;
        editing_mode_ = false;
      }
      active_note_idx_ = -1;
      selected_note_indices.clear();
      folder_overview_mode_ = true;
      load_note_content_for_active();
      request_focus_sidebar_ = true;
      save_index();
    }
    request_hide_folder_notes_ = false;
  }
  if(request_cycle_visible_notes_ != 0)
  {
    if(has_active_note())
    {
      const int count = static_cast<int>(folders_[static_cast<std::size_t>(active_folder_idx_)].notes.size());
      const int next = (active_note_idx_ + request_cycle_visible_notes_ + count) % count;
      focus_visible_note(next, request_cycle_visible_notes_);
    }
    request_cycle_visible_notes_ = 0;
  }
  if(request_focus_active_note_)
  {
    if(has_active_note() && !folders_[static_cast<std::size_t>(active_folder_idx_)].notes[static_cast<std::size_t>(active_note_idx_)].hidden)
    {
      if(folder_overview_mode_)
        pending_focus_note_idx = active_note_idx_;
      else
        search_request_window_focus_ = true;
    }
    else
    {
      request_focus_sidebar_ = true;
    }
    request_focus_active_note_ = false;
  }

  if(request_clear_selection_)
  {
    selected_note_indices.clear();
    selected_stroke_indices.clear();
    pending_focus_note_idx = -1;
    request_clear_selection_ = false;
  }
  drag_hover_folder_idx = -1;

  if(!editing_mode_ && (request_undo_edit_ || request_undo_draw_))
  {
    if(apply_global_undo() && active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
      flash_mark_folder(folders_[(size_t)active_folder_idx_].name, ImVec4(0.86f, 0.25f, 0.25f, 1.0f));
    request_undo_edit_ = false;
    request_redo_edit_ = false;
    request_undo_draw_ = false;
    request_redo_draw_ = false;
  }
  else if(!editing_mode_ && (request_redo_edit_ || request_redo_draw_))
  {
    if(apply_global_redo() && active_folder_idx_ >= 0 && active_folder_idx_ < (int)folders_.size())
      flash_mark_folder(folders_[(size_t)active_folder_idx_].name, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
    request_redo_edit_ = false;
    request_undo_draw_ = false;
    request_redo_draw_ = false;
  }

#if USE_PORTABLE_PATHS
  static bool open_project_picker_popup = false;
  static std::vector<std::filesystem::path> recent_projects_cache;
  if(request_open_project_)
  {
    open_project_picker_popup = true;
    recent_projects_cache = notepp::project::load_recent_projects();
    request_open_project_ = false;
  }
  if(open_project_picker_popup)
  {
    ImGui::OpenPopup(Lang::t("Open Project"));
    open_project_picker_popup = false;
  }
  if(ImGui::BeginPopupModal(Lang::t("Open Project"), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
  {
    std::filesystem::path selected_root;
    ImGui::TextUnformatted(Lang::t("Recent projects:"));
    ImGui::Spacing();
    if(recent_projects_cache.empty())
    {
      ImGui::TextDisabled("  %s", Lang::t("(none)"));
    }
    else
    {
      const float list_w = 420.0f;
      const float line_h = ImGui::GetTextLineHeightWithSpacing();
      const float list_h = std::min((float)recent_projects_cache.size(), 8.0f) * line_h + ImGui::GetStyle().WindowPadding.y;
      if(ImGui::BeginChild("##recent_list", ImVec2(list_w, list_h), true))
      {
        for(const auto &p : recent_projects_cache)
        {
          const std::string label = p.filename().string() + "##" + p.generic_string();
          const bool is_current = (p == config_.dataPath.parent_path());
          if(is_current)
            ImGui::BeginDisabled();
          if(ImGui::Selectable(label.c_str()))
            selected_root = p;
          if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", p.generic_string().c_str());
          if(is_current)
            ImGui::EndDisabled();
        }
      }
      ImGui::EndChild();
    }
    ImGui::Spacing();
    if(ImGui::Button(Lang::t("Browse...")))
    {
      ImGui::CloseCurrentPopup();
      auto picked = notepp::project::select_project_folder();
      if(picked)
        selected_root = *picked;
    }
    ImGui::SameLine();
    if(ImGui::Button(Lang::t("Cancel")))
      ImGui::CloseCurrentPopup();
    if(!selected_root.empty())
    {
      ImGui::CloseCurrentPopup();
      switch_project(selected_root);
    }
    ImGui::EndPopup();
  }
#endif

  // Creation is handled from context menus (right-click).

  if(open_new_folder_popup)
  {
    ImGui::OpenPopup("New Folder");
    open_new_folder_popup = false;
    new_folder_buf[0] = '\0';
  }
  if(open_new_note_popup)
  {
    ImGui::OpenPopup("New Note");
    open_new_note_popup = false;
    new_note_buf[0] = '\0';
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
  if(open_note_color_popup)
  {
    ImGui::OpenPopup("Note Color Sidebar");
    open_note_color_popup = false;
  }
  if(open_paste_note_popup)
  {
    ImGui::OpenPopup("Paste Note");
    open_paste_note_popup = false;
  }
  if(open_rename_image_popup)
  {
    ImGui::OpenPopup("Rename Image");
    open_rename_image_popup = false;
  }
  if(open_restore_bak_popup)
  {
    ImGui::OpenPopup("Restore Removed Notes");
    open_restore_bak_popup = false;
  }

  if(ImGui::BeginPopupModal("Restore Removed Notes", nullptr,
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
  {
    namespace fs = std::filesystem;
    if(bak_candidates.empty())
    {
      ImGui::TextUnformatted(Lang::t("No removed notes found."));
      ImGui::Spacing();
      if(ImGui::Button(Lang::t("Close"))) ImGui::CloseCurrentPopup();
    }
    else
    {
      ImGui::TextUnformatted(Lang::t("Select notes to restore:"));
      ImGui::Spacing();
      ImGui::BeginChild("##bak_list", ImVec2(360.0f, std::min((float)bak_candidates.size() * 22.0f + 8.0f, 240.0f)), true);
      for(int bi = 0; bi < (int)bak_candidates.size(); ++bi)
      {
        const std::string &bak_path = bak_candidates[(size_t)bi];
        std::error_code ec;
        fs::path rel = fs::relative(fs::path(bak_path), config_.dataPath, ec);
        std::string display = ec ? bak_path : rel.string();
        // Strip trailing ".bak" from display name
        if(display.size() > 4 && display.compare(display.size() - 4, 4, ".bak") == 0)
          display.resize(display.size() - 4);
        ImGui::PushID(bi);
        bool checked = bak_selected[(size_t)bi] != 0;
        if(ImGui::Checkbox(display.c_str(), &checked))
          bak_selected[(size_t)bi] = checked ? 1 : 0;
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::Spacing();
      // Select all / deselect all
      if(ImGui::SmallButton(Lang::t("Select All")))
        for(int &s : bak_selected) s = 1;
      ImGui::SameLine();
      if(ImGui::SmallButton(Lang::t("Deselect All")))
        for(int &s : bak_selected) s = 0;
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      if(ImGui::Button(Lang::t("Restore Selected")))
      {
        bool any = false;
        for(int bi = 0; bi < (int)bak_candidates.size(); ++bi)
        {
          if(!bak_selected[(size_t)bi]) continue;
          const std::string &bak_path = bak_candidates[(size_t)bi];
          // Original path is bak_path without the ".bak" suffix
          const std::string orig = bak_path.substr(0, bak_path.size() - 4);
          if(restore_project_backup(orig))
          {
            // Remove from pending-delete list so the app won't clean it up
            pending_fs_delete_paths_.erase(
                std::remove(pending_fs_delete_paths_.begin(), pending_fs_delete_paths_.end(), orig),
                pending_fs_delete_paths_.end());
            any = true;
          }
        }
        if(any)
        {
          sync_project_files();
          save_index();
        }
        bak_candidates.clear();
        bak_selected.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if(ImGui::Button(Lang::t("Cancel"))) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("New Folder"))
  {
    ImGui::SetNextItemWidth(200.0f);
    if(ImGui::InputText(Lang::t("Name"), new_folder_buf, sizeof(new_folder_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(new_folder_buf[0] != '\0')
        perform_workspace_change("Create folder", [&]() {
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
          state_dirty_ = true;
          active_folder_idx_ = (int)folders_.size() - 1;
          active_note_idx_ = -1;
          folder_overview_mode_ = true;
          flash_mark_folder(candidate, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
          load_note_content_for_active();
          save_index();
        });
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
    if(ImGui::InputText(Lang::t("Title"), new_note_buf, sizeof(new_note_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(new_note_buf[0] != '\0')
        perform_workspace_change("Create note", [&]() {
          ensure_default_index();
          int target_folder_idx;
          if(new_note_target_folder_idx == -2)
          {
            // Find or create root "." folder
            target_folder_idx = -1;
            for(int i = 0; i < (int)folders_.size(); ++i)
              if(folders_[(size_t)i].name == ".")
              {
                target_folder_idx = i;
                break;
              }
            if(target_folder_idx < 0)
            {
              folders_.push_back(FolderMeta{".", {}});
              target_folder_idx = (int)folders_.size() - 1;
            }
          }
          else
          {
            if(folders_.empty()) return;
            target_folder_idx =
                (new_note_target_folder_idx >= 0 && new_note_target_folder_idx < (int)folders_.size())
                    ? new_note_target_folder_idx
                    : std::max(0, active_folder_idx_);
          }
          if(target_folder_idx < 0 || target_folder_idx >= (int)folders_.size()) return;
          FolderMeta &f = folders_[(size_t)target_folder_idx];
          NoteMeta n;
          n.id = generate_uuid();
          n.title = make_unique_note_title(target_folder_idx, new_note_buf);
          n.path = make_note_path(f.name, n.title);
          remove_pending_delete_path(n.path);
          f.notes.push_back(std::move(n));
          flash_mark_note(f.notes.back().path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
          flash_mark_folder(f.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
          active_folder_idx_ = target_folder_idx;
          active_note_idx_ = (int)f.notes.size() - 1;
          force_open_folder_idx = target_folder_idx;
          load_note_content_for_active();
          save_index();
        });
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Rename Note Sidebar"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText(Lang::t("Name"), rename_note_buf, sizeof(rename_note_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(rename_note_folder_idx >= 0 && rename_note_idx >= 0)
      {
        perform_workspace_change("Rename note", [&]() {
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
        });
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Rename Folder Sidebar"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText(Lang::t("Name"), rename_folder_buf, sizeof(rename_folder_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(rename_folder_idx >= 0 && rename_folder_idx < (int)folders_.size())
      {
        perform_workspace_change("Rename folder", [&]() {
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
              std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
              moves.reserve(rf.notes.size());
              for(const NoteMeta &note : rf.notes)
              {
                const std::filesystem::path new_path = make_note_path(target_name, note.title);
                std::filesystem::create_directories(new_path.parent_path());
                moves.emplace_back(note.path, new_path);
              }
              if(!move_project_files(moves))
              {
                ImGui::CloseCurrentPopup();
                return;
              }
              for(std::size_t index = 0; index < rf.notes.size(); ++index)
                rf.notes[index].path = moves[index].second.string();
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
        });
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Rename Image"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText(Lang::t("Name"), rename_image_buf, sizeof(rename_image_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      if(rename_image_folder_idx >= 0 && rename_image_folder_idx < (int)folders_.size() &&
         !rename_image_current_path.empty() && rename_image_buf[0] != '\0')
      {
        FolderMeta &rf = folders_[(size_t)rename_image_folder_idx];
        const std::filesystem::path old_path(rename_image_current_path);
        const std::filesystem::path new_path = old_path.parent_path() / rename_image_buf;
        if(new_path != old_path)
        {
          if(move_project_file(old_path, new_path))
          {
            for(auto &img_path : rf.images)
            {
              if(img_path == rename_image_current_path)
              {
                img_path = new_path.string();
                break;
              }
            }
            invalidate_folder_image_cache(rf.name);
            save_index();
          }
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Note Color Sidebar"))
  {
    if(color_note_folder_idx >= 0 && color_note_folder_idx < (int)folders_.size())
    {
      FolderMeta &cf = folders_[(size_t)color_note_folder_idx];
      if(color_note_idx >= 0 && color_note_idx < (int)cf.notes.size())
      {
        NoteMeta &cn = cf.notes[(size_t)color_note_idx];
        bool changed = false;
        if(ImGui::Checkbox(Lang::t("Use default"), &note_color_use_default))
        {
          changed = true;
        }
        if(!note_color_use_default)
        {
          if(ImGui::ColorEdit3(Lang::t("Color"), note_color_buf, ImGuiColorEditFlags_NoInputs))
          {
            changed = true;
          }
        }
        if(changed)
        {
          perform_workspace_change("Set note color", [&]() {
            cn.use_custom_color = !note_color_use_default;
            if(!note_color_use_default)
            {
              cn.color_r = clamp01f(note_color_buf[0]);
              cn.color_g = clamp01f(note_color_buf[1]);
              cn.color_b = clamp01f(note_color_buf[2]);
            }
            flash_mark_note(cn.path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
            save_index();
          });
        }
      }
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopup("Paste Note"))
  {
    ImGui::SetNextItemWidth(240.0f);
    if(ImGui::InputText(Lang::t("Name"), paste_note_buf, sizeof(paste_note_buf),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
      pending_paste_note_folder_idx = std::max(0, paste_target_folder_idx);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if(ImGui::BeginPopupContextWindow("ExplorerContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
  {
    if(ImGui::MenuItem(Lang::t("Reveal in File Explorer")))
      open_directory(config_.dataPath);
    ImGui::Separator();
    if(ImGui::MenuItem(Lang::t("Enable Git Sync"), nullptr, git_sync_enabled_))
      set_git_sync_enabled(!git_sync_enabled_);
    if(git_sync_enabled_ && ImGui::MenuItem(Lang::t("Sync now"), nullptr, false, !git_sync_in_progress_))
      begin_git_operation(true);
    if(git_sync_enabled_ && git_status_available_ &&
       ImGui::MenuItem(Lang::t("Retry Git Sync"), nullptr, false, !git_sync_in_progress_))
      begin_git_operation(true);
    if(git_status_available_)
    {
      ImGui::TextDisabled("%s", git_status_.summary.c_str());
      if(!git_last_attempt_.empty())
        ImGui::TextDisabled("%s: %s", Lang::t("Last Git Sync attempt"), git_last_attempt_.c_str());
      if(!git_status_.detail.empty())
      {
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextDisabled("%s", git_status_.detail.c_str());
        ImGui::PopTextWrapPos();
      }
    }
    if(!app_settings_error_.empty() || !project_settings_error_.empty())
    {
      const std::string &error = !project_settings_error_.empty()
                                     ? project_settings_error_
                                     : app_settings_error_;
      ImGui::PushTextWrapPos(420.0f);
      ImGui::TextDisabled("%s: %s", Lang::t("Settings error"), error.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
#if USE_PORTABLE_PATHS
    if(ImGui::MenuItem(Lang::t("Open project...")))
      request_open_project_ = true;
    ImGui::Separator();
#endif
    if(ImGui::MenuItem(Lang::t("Find in project...")))
    {
      request_open_project_search_ = true;
    }
    if(ImGui::MenuItem(Lang::t("New folder")))
    {
      open_new_folder_popup = true;
      new_folder_parent_idx = -1;
    }
    if(ImGui::MenuItem(Lang::t("New note")))
    {
      open_new_note_popup = true;
      new_note_target_folder_idx = active_folder_idx_;
    }
    if(ImGui::MenuItem(Lang::t("New note in root")))
    {
      open_new_note_popup = true;
      new_note_target_folder_idx = -2;
    }
    if(ImGui::MenuItem(Lang::t("Paste note"), nullptr, false, g_has_copied_note))
    {
      paste_target_folder_idx = active_folder_idx_;
      std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
      open_paste_note_popup = true;
    }
    ImGui::Separator();
    if(ImGui::MenuItem(Lang::t("Demo")))
      open_or_create_readme();
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
      CopiedNoteItem ci;
      ci.title = n.title;
      ci.content = std::move(content);
      ci.font_path = n.font_path;
      ci.font_size = n.font_size;
      ci.use_custom_color = n.use_custom_color;
      ci.color_r = n.color_r;
      ci.color_g = n.color_g;
      ci.color_b = n.color_b;
      ci.width = n.width;
      ci.height = n.height;
      ci.has_layout = n.has_layout;
      ci.always_on_top = n.always_on_top;
      g_copied_notes_batch.push_back(std::move(ci));
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
      if(!(f.name == root || StringUtils::starts_with(f.name, root + "/"))) continue;
      CopiedFolderEntry e;
      e.rel_path = f.name.substr(root.size()); // "" or "/child..."
      for(const NoteMeta &n : f.notes)
      {
        std::ifstream in_note(n.path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in_note)), std::istreambuf_iterator<char>());
        CopiedNoteItem ci;
        ci.title = n.title;
        ci.content = std::move(content);
        ci.font_path = n.font_path;
        ci.font_size = n.font_size;
        ci.use_custom_color = n.use_custom_color;
        ci.color_r = n.color_r;
        ci.color_g = n.color_g;
        ci.color_b = n.color_b;
        ci.width = n.width;
        ci.height = n.height;
        ci.has_layout = n.has_layout;
        ci.always_on_top = n.always_on_top;
        e.notes.push_back(std::move(ci));
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
        perform_workspace_change("Paste folder", [&]() {
          const std::string dst_parent = folders_[(size_t)active_folder_idx_].name;
          const std::string new_root = make_unique_folder_path(dst_parent, folder_base_name(g_copied_folder_root_name));
          for(const CopiedFolderEntry &e : g_copied_folder_entries)
          {
            FolderMeta nf;
            nf.name = new_root + e.rel_path;
            std::unordered_set<std::string> used_titles;
            for(const CopiedNoteItem &cn : e.notes)
            {
              NoteMeta nn;
              nn.id = generate_uuid();
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
              nn.font_path = cn.font_path;
              nn.font_size = cn.font_size;
              nn.use_custom_color = cn.use_custom_color;
              nn.color_r = cn.color_r;
              nn.color_g = cn.color_g;
              nn.color_b = cn.color_b;
              nn.width = cn.width;
              nn.height = cn.height;
              nn.has_layout = cn.has_layout;
              nn.always_on_top = cn.always_on_top;
              remove_pending_delete_path(nn.path);
              write_text_file(nn.path, cn.content);
              nf.notes.push_back(std::move(nn));
            }
            folders_.push_back(std::move(nf));
          }
          flash_mark_folder(new_root, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
          save_index();
        });
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
    const ImVec4 flash_col = flash_current_color(flash_key_folder(f.name), now_time);
    ImVec4 tree_text_col = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
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
        if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsPreview())
        {
          drag_hover_folder_idx = fi;
        }
        if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsDelivery())
        {
          const ImVec2 p = static_cast<const ExplorerNoteDragPayload *>(payload->Data)->indices;
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
      const int prev_folder = active_folder_idx_;
      state_dirty_ = true;
      active_folder_idx_ = fi;
      selected_note_indices.clear();
      selected_stroke_indices.clear();
      folder_overview_mode_ = true;
      editing_mode_ = false;
      request_exit_edit_mode_ = false;
      request_cancel_draw_tools_ = true;
      sidebar_last_selected_was_folder = true;
      if(fi != prev_folder)
        apply_folder_settings(fi);
      save_index();
    }

    const std::string folder_popup_id = "FolderCtx##" + std::to_string(fi);
    if(ImGui::BeginPopupContextItem(folder_popup_id.c_str(), ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem(Lang::t("New note")))
      {
        new_note_target_folder_idx = fi;
        open_new_note_popup = true;
      }
      if(ImGui::MenuItem(Lang::t("New folder")))
      {
        open_new_folder_popup = true;
        new_folder_parent_idx = fi;
      }
      if(ImGui::MenuItem(Lang::t("Rename")))
      {
        rename_folder_idx = fi;
        std::snprintf(rename_folder_buf, sizeof(rename_folder_buf), "%s", folder_base_name(f.name).c_str());
        open_rename_folder_popup = true;
      }
      if(ImGui::MenuItem(Lang::t("Paste note"), nullptr, false, g_has_copied_note))
      {
        paste_target_folder_idx = fi;
        std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
        open_paste_note_popup = true;
      }
      if(ImGui::MenuItem(Lang::t("Delete")))
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
        const std::string note_item_label =
            n.title + "###ExplorerNote_" + std::to_string(fi) + "_" + std::to_string(ni);
        const ImVec4 note_flash_col = flash_current_color(flash_key_note(n.path), now_time);
        ImVec4 note_text_col = folder_accent_color(n.use_custom_color, n.color_r, n.color_g, n.color_b, sidebar_style);
        if(note_flash_col.w > 0.0f) note_text_col = mix_color(note_text_col, note_flash_col, 0.78f);
        note_text_col.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_Text, note_text_col);
        if(ImGui::Selectable(note_item_label.c_str(), note_selected))
        {
          const bool ctrl = ImGui::GetIO().KeyCtrl;
          const bool shift = ImGui::GetIO().KeyShift;
          const int prev_folder = active_folder_idx_;
          state_dirty_ = true;
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
          sidebar_last_selected_was_folder = false;
          if(fi != prev_folder)
            apply_folder_settings(fi);
          load_note_content_for_active();
          save_index();
        }
        ImGui::PopStyleColor();
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
            if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsPreview())
            {
              drag_hover_folder_idx = fi;
            }
            if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsDelivery())
            {
              const ImVec2 p = static_cast<const ExplorerNoteDragPayload *>(payload->Data)->indices;
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
          ExplorerNoteDragPayload payload;
          payload.indices = ImVec2((float)fi, (float)ni);
          std::error_code link_ec;
          std::filesystem::path link_path = std::filesystem::relative(n.path, config_.dataPath, link_ec);
          const std::string link_reference = (!link_ec && !link_path.empty())
                                                  ? link_path.generic_string()
                                                  : std::filesystem::path(n.path).filename().generic_string();
          std::snprintf(payload.reference, sizeof(payload.reference), "%s", link_reference.c_str());
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
          if(ImGui::MenuItem(Lang::t("Rename")))
          {
            rename_note_folder_idx = fi;
            rename_note_idx = ni;
            std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s", n.title.c_str());
            open_rename_note_popup = true;
          }
          if(ImGui::MenuItem(Lang::t("Edit note")))
          {
            const int prev_folder = active_folder_idx_;
            state_dirty_ = true;
            active_folder_idx_ = fi;
            active_note_idx_ = ni;
            if(fi != prev_folder)
              apply_folder_settings(fi);
            load_note_content_for_active();
            editing_mode_ = true;
            request_exit_edit_mode_ = false;
            force_open_folder_idx = fi;
            save_index();
          }
          if(ImGui::MenuItem(Lang::t("Set note color...")))
          {
            color_note_folder_idx = fi;
            color_note_idx = ni;
            note_color_use_default = !n.use_custom_color;
            note_color_buf[0] = n.color_r;
            note_color_buf[1] = n.color_g;
            note_color_buf[2] = n.color_b;
            open_note_color_popup = true;
          }
          if(ImGui::MenuItem(Lang::t("Reset note color"), nullptr, false, n.use_custom_color))
          {
            push_sidebar_snapshot();
            n.use_custom_color = false;
            flash_mark_note(n.path, ImVec4(0.86f, 0.25f, 0.25f, 1.0f));
            save_index();
          }
          if(ImGui::MenuItem(multi_selected_here ? Lang::t("Copy selected notes") : Lang::t("Copy note")))
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
          if(ImGui::MenuItem(Lang::t("Paste note"), nullptr, false, g_has_copied_note))
          {
            paste_target_folder_idx = fi;
            std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
            open_paste_note_popup = true;
          }
          if(ImGui::MenuItem(Lang::t("New note")))
          {
            new_note_target_folder_idx = fi;
            open_new_note_popup = true;
          }
          if(ImGui::MenuItem(multi_selected_here ? Lang::t("Remove selected notes") : Lang::t("Remove note")))
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

      // --- Image files in folder ---
      {
        const std::filesystem::path folder_dir = config_.dataPath / f.name;
        const auto &img_entries = get_folder_images(f.name, folder_dir);
        for(int img_idx = 0; img_idx < (int)img_entries.size(); ++img_idx)
        {
          const ExplorerImageEntry &img = img_entries[(size_t)img_idx];
          ImGui::PushID(img_idx + 0x40000);

          const auto tex = MarkdownView::get_or_load_sidebar_thumbnail(img.path);
          const float row_h = ImGui::GetTextLineHeightWithSpacing();
          const float thumb_h = ImGui::GetTextLineHeight();
          const float thumb_w = (tex.valid && tex.height > 0.0f)
                                    ? thumb_h * tex.width / tex.height
                                    : thumb_h;

          // Full-row Selectable (blank label — content drawn via drawlist)
          ImGui::Selectable("##imgrow", false, 0, ImVec2(0.0f, row_h));
          const ImVec2 item_min = ImGui::GetItemRectMin();
          const ImVec2 item_max = ImGui::GetItemRectMax();

          // Draw thumbnail
          if(tex.valid)
          {
            const float y_off = (row_h - thumb_h) * 0.5f;
            ImGui::GetWindowDrawList()->AddImage(
                tex.id,
                ImVec2(item_min.x + 2.0f, item_min.y + y_off),
                ImVec2(item_min.x + 2.0f + thumb_w, item_min.y + y_off + thumb_h));
          }
          else
          {
            // Placeholder box when image not loaded yet
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(item_min.x + 2.0f, item_min.y + (row_h - thumb_h) * 0.5f),
                ImVec2(item_min.x + 2.0f + thumb_h, item_min.y + (row_h + thumb_h) * 0.5f),
                ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 0.5f)), 2.0f);
          }

          // Draw filename text (clipped)
          const float text_x = item_min.x + thumb_w + 6.0f;
          const float text_y = item_min.y + (row_h - ImGui::GetFontSize()) * 0.5f;
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->PushClipRect(ImVec2(text_x, item_min.y), item_max, true);
          dl->AddText(ImVec2(text_x, text_y),
                      ImGui::GetColorU32(ImVec4(0.78f, 0.82f, 0.88f, 1.0f)),
                      img.name.c_str());
          dl->PopClipRect();

          // Hover: large thumbnail tooltip
          if(ImGui::IsItemHovered() && tex.valid)
          {
            ImGui::BeginTooltip();
            const float max_sz = 220.0f;
            float tw = tex.width, th = tex.height;
            if(tw > max_sz)
            {
              th = th * max_sz / tw;
              tw = max_sz;
            }
            if(th > max_sz)
            {
              tw = tw * max_sz / th;
              th = max_sz;
            }
            ImGui::Image(tex.id, ImVec2(tw, th));
            ImGui::TextUnformatted(img.name.c_str());
            ImGui::EndTooltip();
          }

          // Drag source: payload = absolute image path (null-terminated)
          if(ImGui::BeginDragDropSource())
          {
            ImGui::SetDragDropPayload("NOTEPP_IMAGE_INSERT",
                                      img.path.c_str(), img.path.size() + 1);
            if(tex.valid)
            {
              const float drag_h = 40.0f;
              const float drag_w = tex.height > 0.0f
                                       ? drag_h * tex.width / tex.height
                                       : drag_h;
              ImGui::Image(tex.id, ImVec2(drag_w, drag_h));
              ImGui::SameLine();
            }
            ImGui::TextUnformatted(img.name.c_str());
            ImGui::EndDragDropSource();
          }

          // Context menu
          const std::string img_ctx_id =
              "ImgCtx##" + std::to_string(fi) + "_" + std::to_string(img_idx);
          if(ImGui::BeginPopupContextItem(img_ctx_id.c_str(),
                                          ImGuiPopupFlags_MouseButtonRight))
          {
            if(ImGui::MenuItem(Lang::t("Reveal in File Explorer")))
              reveal_in_file_explorer(img.path);
            ImGui::Separator();
            if(ImGui::MenuItem(Lang::t("Rename")))
            {
              rename_image_folder_idx = fi;
              rename_image_current_path = img.path;
              std::snprintf(rename_image_buf, sizeof(rename_image_buf), "%s",
                            std::filesystem::path(img.path).filename().string().c_str());
              open_rename_image_popup = true;
            }
            if(ImGui::BeginMenu(Lang::t("Move to folder")))
            {
              for(int dst_fi = 0; dst_fi < (int)folders_.size(); ++dst_fi)
              {
                if(dst_fi == fi) continue;
                const std::string dst_display = folders_[(size_t)dst_fi].name == "."
                                                    ? "(root)"
                                                    : folder_base_name(folders_[(size_t)dst_fi].name);
                if(ImGui::MenuItem(dst_display.c_str()))
                {
                  pending_move_image_src_fi = fi;
                  pending_move_image_dst_fi = dst_fi;
                  pending_move_image_path = img.path;
                }
              }
              ImGui::EndMenu();
            }
            ImGui::Separator();
            if(ImGui::MenuItem(Lang::t("Delete image")))
            {
              const std::string img_path = img.path;
              if(soft_delete_project_file(img_path))
              {
                f.images.erase(
                    std::remove(f.images.begin(), f.images.end(), img_path),
                    f.images.end());
                invalidate_folder_image_cache(f.name);
                queue_pending_delete_path(img_path);
                save_index();
              }
            }
            ImGui::EndPopup();
          }

          ImGui::PopID();
        }
      }

      // --- Font files in folder ---
      {
        const std::filesystem::path folder_dir = config_.dataPath / f.name;
        const auto &font_entries = get_folder_fonts(f.name, folder_dir);
        for(int font_idx = 0; font_idx < (int)font_entries.size(); ++font_idx)
        {
          const ExplorerFontEntry &fnt = font_entries[(size_t)font_idx];
          ImGui::PushID(font_idx + 0x80000);

          const float row_h = ImGui::GetTextLineHeightWithSpacing();
          const float badge_w = ImGui::GetTextLineHeight();

          ImGui::Selectable("##fontrow", false, 0, ImVec2(0.0f, row_h));
          const ImVec2 item_min = ImGui::GetItemRectMin();
          const ImVec2 item_max = ImGui::GetItemRectMax();

          // "Aa" badge
          ImDrawList *fdl = ImGui::GetWindowDrawList();
          fdl->AddRectFilled(
              ImVec2(item_min.x + 2.0f, item_min.y + (row_h - badge_w) * 0.5f),
              ImVec2(item_min.x + 2.0f + badge_w, item_min.y + (row_h + badge_w) * 0.5f),
              ImGui::GetColorU32(ImVec4(0.35f, 0.55f, 0.85f, 0.7f)), 2.0f);
          const float badge_font_sz = badge_w * 0.6f;
          fdl->AddText(
              nullptr, badge_font_sz,
              ImVec2(item_min.x + 2.0f + badge_w * 0.12f,
                     item_min.y + (row_h - badge_font_sz) * 0.5f),
              ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f)), "Aa");

          // Filename text
          const float text_x = item_min.x + badge_w + 6.0f;
          const float text_y = item_min.y + (row_h - ImGui::GetFontSize()) * 0.5f;
          fdl->PushClipRect(ImVec2(text_x, item_min.y), item_max, true);
          fdl->AddText(ImVec2(text_x, text_y),
                       ImGui::GetColorU32(ImVec4(0.78f, 0.82f, 0.88f, 1.0f)),
                       fnt.name.c_str());
          fdl->PopClipRect();

          // Tooltip
          if(ImGui::IsItemHovered())
          {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(fnt.name.c_str());
            ImGui::TextDisabled("%s", Lang::t("Drag to set font"));
            ImGui::EndTooltip();
          }

          // Drag source
          if(ImGui::BeginDragDropSource())
          {
            ImGui::SetDragDropPayload("NOTEPP_FONT_SET",
                                      fnt.path.c_str(), fnt.path.size() + 1);
            ImGui::TextUnformatted(fnt.name.c_str());
            ImGui::TextDisabled("%s", Lang::t("Drop to set font"));
            ImGui::EndDragDropSource();
          }

          // Context menu
          const std::string fnt_ctx_id =
              "FntCtx##" + std::to_string(fi) + "_" + std::to_string(font_idx);
          if(ImGui::BeginPopupContextItem(fnt_ctx_id.c_str(),
                                          ImGuiPopupFlags_MouseButtonRight))
          {
            if(ImGui::MenuItem(Lang::t("Reveal in File Explorer")))
              reveal_in_file_explorer(fnt.path);
            ImGui::EndPopup();
          }

          ImGui::PopID();
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
  // Find root "." folder index for the root drop zone
  int root_folder_idx = -1;
  {
    auto r_it = folder_children.find(std::string{});
    if(r_it != folder_children.end())
      for(int rfi_scan : r_it->second)
        if(folders_[(size_t)rfi_scan].name == ".")
        {
          root_folder_idx = rfi_scan;
          break;
        }
  }
  // Root section header: always rendered, accepts note drops, has "New note" context menu
  {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float header_h = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 header_min = ImGui::GetCursorScreenPos();
    const ImVec2 header_max(header_min.x + avail_w, header_min.y + header_h);
    ImGui::InvisibleButton("##RootHeader", nonzero_invisible_button_size(avail_w, header_h));
    ImDrawList *root_dl = ImGui::GetWindowDrawList();
    // Draw faint "Root" label
    root_dl->AddText(
        ImVec2(header_min.x + ImGui::GetStyle().ItemSpacing.x, header_min.y + (header_h - ImGui::GetTextLineHeight()) * 0.5f),
        ImGui::GetColorU32(ImVec4(0.50f, 0.52f, 0.55f, 0.85f)),
        Lang::t("Root"));
    const std::string sync_state_name = git_status_available_
                                            ? std::string(notepp::git_sync::state_name(git_status_.state))
                                            : std::string{};
    const char *sync_label = !git_sync_enabled_
                                 ? Lang::t("Git Sync disabled")
                             : git_sync_in_progress_
                                 ? Lang::t("Syncing")
                             : git_status_available_
                                 ? Lang::t(sync_state_name.c_str())
                                 : Lang::t("Git status unknown");
    const ImVec4 sync_color = !git_sync_enabled_
                                   ? ImVec4(0.72f, 0.72f, 0.74f, 1.0f)
                               : git_sync_in_progress_
                                   ? ImVec4(0.95f, 0.72f, 0.24f, 1.0f)
                               : git_status_available_ && git_status_.state == notepp::git_sync::SyncState::clean
                                   ? ImVec4(0.30f, 0.78f, 0.42f, 1.0f)
                                   : ImVec4(0.72f, 0.72f, 0.74f, 1.0f);
    const ImVec2 sync_size = ImGui::CalcTextSize(sync_label);
    root_dl->AddText(
        ImVec2(header_max.x - ImGui::GetStyle().ItemSpacing.x - sync_size.x,
               header_min.y + (header_h - ImGui::GetTextLineHeight()) * 0.5f),
        ImGui::GetColorU32(sync_color), sync_label);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && git_status_available_)
      ImGui::SetTooltip("%s%s%s%s%s", git_status_.summary.c_str(),
                        git_status_.detail.empty() ? "" : "\n", git_status_.detail.c_str(),
                        git_last_attempt_.empty() ? "" : "\nLast attempt: ", git_last_attempt_.c_str());
    // Highlight on drag hover (-99 is sentinel for root zone)
    if(drag_hover_folder_idx == -99)
    {
      root_dl->AddRectFilled(header_min, header_max, ImGui::GetColorU32(sidebar_hover_fill), 3.0f);
      root_dl->AddRect(header_min, header_max, ImGui::GetColorU32(sidebar_hover_stroke), 3.0f, 0, 1.2f);
    }
    // Drop target: accept notes dropped onto the root header
    if(ImGui::BeginDragDropTarget())
    {
      if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NOTEPP_NOTE_MOVE", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
      {
        if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsPreview())
          drag_hover_folder_idx = -99;
        if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsDelivery())
        {
          const ImVec2 p = static_cast<const ExplorerNoteDragPayload *>(payload->Data)->indices;
          pending_move_source_folder_idx = (int)p.x;
          pending_move_target_folder_idx = (root_folder_idx >= 0) ? root_folder_idx : -2;
          pending_move_note_indices.clear();
          const int dragged_note_idx = (int)p.y;
          if(pending_move_source_folder_idx >= 0 && pending_move_source_folder_idx < (int)folders_.size())
          {
            if(pending_move_source_folder_idx == active_folder_idx_ &&
               selected_note_indices.count(dragged_note_idx) != 0 &&
               selected_note_indices.size() > 1)
            {
              for(int idx : selected_note_indices)
                if(idx >= 0 && idx < (int)folders_[(size_t)pending_move_source_folder_idx].notes.size())
                  pending_move_note_indices.push_back(idx);
            }
            else
              pending_move_note_indices.push_back(dragged_note_idx);
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
    // Context menu on root header
    if(ImGui::BeginPopupContextItem("RootHeaderCtx", ImGuiPopupFlags_MouseButtonRight))
    {
      if(ImGui::MenuItem(Lang::t("New note")))
      {
        new_note_target_folder_idx = (root_folder_idx >= 0) ? root_folder_idx : -2;
        open_new_note_popup = true;
      }
      if(ImGui::MenuItem(Lang::t("Paste note"), nullptr, false, g_has_copied_note && root_folder_idx >= 0))
      {
        paste_target_folder_idx = root_folder_idx;
        std::snprintf(paste_note_buf, sizeof(paste_note_buf), "%s", g_copied_note_title.c_str());
        open_paste_note_popup = true;
      }
      ImGui::EndPopup();
    }
  }
  auto roots_it = folder_children.find(std::string{});
  if(roots_it != folder_children.end())
  {
    for(int rfi : roots_it->second)
    {
      if(folders_[(size_t)rfi].name == ".")
      {
        // Root notes live directly in the notes directory — render as flat list, no folder header
        FolderMeta &rf = folders_[(size_t)rfi];
        for(int ni = 0; ni < (int)rf.notes.size(); ++ni)
        {
          NoteMeta &n = rf.notes[(size_t)ni];
          const bool note_sel = (rfi == active_folder_idx_) && selected_note_indices.count(ni);
          const std::string label = n.title + "###RootNote_" + std::to_string(rfi) + "_" + std::to_string(ni);
          const ImVec4 note_flash_col = flash_current_color(flash_key_note(n.path), now_time);
          ImVec4 note_text_col = folder_accent_color(n.use_custom_color, n.color_r, n.color_g, n.color_b, sidebar_style);
          if(note_flash_col.w > 0.0f) note_text_col = mix_color(note_text_col, note_flash_col, 0.78f);
          note_text_col.w = 1.0f;
          ImGui::PushStyleColor(ImGuiCol_Text, note_text_col);
          if(ImGui::Selectable(label.c_str(), note_sel))
          {
            const int prev_folder = active_folder_idx_;
            state_dirty_ = true;
            active_folder_idx_ = rfi;
            active_note_idx_ = ni;
            n.hidden = false;
            selected_note_indices.clear();
            selected_note_indices.insert(ni);
            selected_stroke_indices.clear();
            pending_focus_note_idx = ni;
            force_open_folder_idx = rfi;
            editing_mode_ = false;
            request_exit_edit_mode_ = false;
            sidebar_last_selected_was_folder = false;
            if(rfi != prev_folder)
              apply_folder_settings(rfi);
            load_note_content_for_active();
            save_index();
          }
          ImGui::PopStyleColor();
          folder_note_row_rects[(size_t)rfi].push_back(
              SidebarRect{ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true});
          if(drag_hover_folder_idx == rfi)
          {
            ImDrawList *ndl = ImGui::GetWindowDrawList();
            ndl->AddRectFilled(
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(),
                ImGui::GetColorU32(sidebar_note_hover_fill),
                2.0f);
          }
          // Drop target: dropping a note onto a root note moves it to root
          if(ImGui::BeginDragDropTarget())
          {
            if(const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NOTEPP_NOTE_MOVE", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
              if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsPreview())
                drag_hover_folder_idx = rfi;
              if(payload->DataSize == (int)sizeof(ExplorerNoteDragPayload) && payload->IsDelivery())
              {
                const ImVec2 p = static_cast<const ExplorerNoteDragPayload *>(payload->Data)->indices;
                pending_move_source_folder_idx = (int)p.x;
                pending_move_target_folder_idx = rfi;
                pending_move_note_indices.clear();
                const int dragged_note_idx = (int)p.y;
                if(pending_move_source_folder_idx >= 0 && pending_move_source_folder_idx < (int)folders_.size())
                {
                  if(pending_move_source_folder_idx == active_folder_idx_ &&
                     selected_note_indices.count(dragged_note_idx) != 0 &&
                     selected_note_indices.size() > 1)
                  {
                    for(int idx : selected_note_indices)
                      if(idx >= 0 && idx < (int)folders_[(size_t)pending_move_source_folder_idx].notes.size())
                        pending_move_note_indices.push_back(idx);
                  }
                  else
                    pending_move_note_indices.push_back(dragged_note_idx);
                }
              }
            }
            ImGui::EndDragDropTarget();
          }
          if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          {
            rename_note_folder_idx = rfi;
            rename_note_idx = ni;
            std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s", n.title.c_str());
            open_rename_note_popup = true;
          }
          // Drag source: root notes can be dragged to other folders
          if(ImGui::BeginDragDropSource())
          {
            ExplorerNoteDragPayload note_payload;
            note_payload.indices = ImVec2((float)rfi, (float)ni);
            std::error_code link_ec;
            std::filesystem::path link_path = std::filesystem::relative(n.path, config_.dataPath, link_ec);
            const std::string link_reference = (!link_ec && !link_path.empty())
                                                    ? link_path.generic_string()
                                                    : std::filesystem::path(n.path).filename().generic_string();
            std::snprintf(note_payload.reference, sizeof(note_payload.reference), "%s", link_reference.c_str());
            ImGui::SetDragDropPayload("NOTEPP_NOTE_MOVE", &note_payload, sizeof(note_payload));
            if(selected_note_indices.count(ni) != 0 && rfi == active_folder_idx_ && selected_note_indices.size() > 1)
              ImGui::Text("%zu notes", selected_note_indices.size());
            else
              ImGui::TextUnformatted(n.title.c_str());
            ImGui::EndDragDropSource();
          }
          const std::string root_note_ctx = "RootNoteCtx##" + std::to_string(rfi) + "_" + std::to_string(ni);
          if(ImGui::BeginPopupContextItem(root_note_ctx.c_str(), ImGuiPopupFlags_MouseButtonRight))
          {
            if(ImGui::MenuItem(Lang::t("Rename")))
            {
              rename_note_folder_idx = rfi;
              rename_note_idx = ni;
              std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s", n.title.c_str());
              open_rename_note_popup = true;
            }
            if(ImGui::MenuItem(Lang::t("Remove note")))
            {
              pending_delete_note_folder_idx = rfi;
              pending_delete_note_indices.clear();
              pending_delete_note_indices.push_back(ni);
              pending_delete_note_idx = ni;
            }
            ImGui::EndPopup();
          }
        }
      }
      else
      {
        render_folder_node(render_folder_node, rfi);
      }
    }
  }

  // Process OS-level file drops (SDL_DROPFILE) onto the Explorer panel
  if(!git_sync_in_progress_ && !close_requested_ && !pending_dropped_files_.empty())
  {
    for(const auto &drop : pending_dropped_files_)
    {
      if(drop.mouse_x < (int)explorer_w && (is_image_file_ext(drop.path) || is_font_file_ext(drop.path)))
      {
        // Find the folder row the cursor was over
        int target_fi = active_folder_idx_;
        for(int i = 0; i < (int)folder_row_rects.size(); ++i)
        {
          const SidebarRect &r = folder_row_rects[(size_t)i];
          if(r.valid && drop.mouse_y >= (int)r.min.y && drop.mouse_y <= (int)r.max.y)
          {
            target_fi = i;
            break;
          }
        }
        if(target_fi >= 0 && target_fi < (int)folders_.size())
        {
          const FolderMeta &tf = folders_[(size_t)target_fi];
          const std::filesystem::path folder_dir = config_.dataPath / tf.name;
          if(is_image_file_ext(drop.path))
          {
            const std::string dest = copy_image_to_folder(drop.path, folder_dir);
            if(!dest.empty())
            {
              invalidate_folder_image_cache(tf.name);
              flash_mark_folder(tf.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
            }
          }
          else
          {
            const std::string dest = copy_font_to_folder(drop.path, folder_dir);
            if(!dest.empty())
            {
              invalidate_folder_font_cache(tf.name);
              flash_mark_folder(tf.name, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
            }
          }
        }
      }
    }
    pending_dropped_files_.clear();
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
    state_dirty_ = true;
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
        const std::string del_path = df.notes[(size_t)ni].path;
        if(!del_path.empty() && !soft_delete_project_file(del_path)) continue;
        queue_pending_delete_path(del_path);
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
    state_dirty_ = true;
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
        if(fn == prefix || StringUtils::starts_with(fn, prefix + "/")) to_remove.push_back(i);
      }
      std::sort(to_remove.begin(), to_remove.end(), std::greater<int>());
      for(int idx : to_remove)
      {
        FolderMeta &df = folders_[(size_t)idx];
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
        for(const NoteMeta &note : df.notes)
          if(!note.path.empty()) moves.emplace_back(note.path, note.path + ".bak");
        if(!move_project_files(moves)) continue;
        for(const NoteMeta &note : df.notes) queue_pending_delete_path(note.path);
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
    if(!folders_.empty())
    {
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
        new_note.id = generate_uuid();
        new_note.title = candidate;
        new_note.path = make_note_path(pf.name, candidate);
        new_note.font_path = items.front().font_path;
        new_note.font_size = items.front().font_size;
        new_note.use_custom_color = items.front().use_custom_color;
        new_note.color_r = items.front().color_r;
        new_note.color_g = items.front().color_g;
        new_note.color_b = items.front().color_b;
        new_note.width = items.front().width;
        new_note.height = items.front().height;
        new_note.has_layout = items.front().has_layout;
        new_note.always_on_top = items.front().always_on_top;
        remove_pending_delete_path(new_note.path);
        pf.notes.push_back(new_note);
        flash_mark_note(new_note.path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));

        write_text_file(new_note.path, items.front().content);
      }
      else
      {
        for(const auto &ci : items)
        {
          const std::string candidate = make_unique_note_title(fi, ci.title.empty() ? "Note" : ci.title);
          NoteMeta new_note;
          new_note.id = generate_uuid();
          new_note.title = candidate;
          new_note.path = make_note_path(pf.name, candidate);
          new_note.font_path = ci.font_path;
          new_note.font_size = ci.font_size;
          new_note.use_custom_color = ci.use_custom_color;
          new_note.color_r = ci.color_r;
          new_note.color_g = ci.color_g;
          new_note.color_b = ci.color_b;
          new_note.width = ci.width;
          new_note.height = ci.height;
          new_note.has_layout = ci.has_layout;
          new_note.always_on_top = ci.always_on_top;
          remove_pending_delete_path(new_note.path);
          pf.notes.push_back(new_note);
          flash_mark_note(new_note.path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));

          write_text_file(new_note.path, ci.content);
        }
      }

      {
        const int prev_folder = active_folder_idx_;
        active_folder_idx_ = fi;
        active_note_idx_ = (int)pf.notes.size() - 1;
        force_open_folder_idx = fi;
        if(fi != prev_folder)
          apply_folder_settings(fi);
        load_note_content_for_active();
        editing_mode_ = false;
        request_exit_edit_mode_ = false;
        save_index();
        flash_mark_folder(pf.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
      }
    } // end if(!folders_.empty())
    pending_paste_note_folder_idx = -1;
    paste_target_folder_idx = -1;
  }
  // Resolve sentinel -2: find or create root "." folder for moves targeting root
  if(pending_move_target_folder_idx == -2)
  {
    int rfIdx = -1;
    for(int i = 0; i < (int)folders_.size(); ++i)
      if(folders_[(size_t)i].name == ".")
      {
        rfIdx = i;
        break;
      }
    if(rfIdx < 0)
    {
      folders_.push_back(FolderMeta{".", {}});
      rfIdx = (int)folders_.size() - 1;
    }
    pending_move_target_folder_idx = rfIdx;
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
        if(!move_project_file(nm.path, new_path))
        {
          src.notes.push_back(std::move(nm));
          continue;
        }
        nm.path = new_path;
        remove_pending_delete_path(new_path);
        nm.hidden = false;
        dst.notes.push_back(std::move(nm));
        flash_mark_note(dst.notes.back().path, ImVec4(0.22f, 0.62f, 0.95f, 1.0f));
      }

      // Source folder may become empty; this is now a valid state.

      const int prev_folder = active_folder_idx_;
      active_folder_idx_ = dst_fi;
      active_note_idx_ = dst.notes.empty() ? -1 : ((int)dst.notes.size() - 1);
      selected_note_indices.clear();
      selected_stroke_indices.clear();
      if(active_note_idx_ >= 0) selected_note_indices.insert(active_note_idx_);
      pending_focus_note_idx = active_note_idx_;
      force_open_folder_idx = dst_fi;
      if(dst_fi != prev_folder)
        apply_folder_settings(dst_fi);
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
      if(!(dst_name == src_name || StringUtils::starts_with(dst_name, src_name + "/")))
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
          if(fn == src_name || StringUtils::starts_with(fn, src_name + "/")) affected.push_back(i);
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
          std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
          moves.reserve(mf.notes.size());
          for(const NoteMeta &note : mf.notes)
          {
            const std::filesystem::path new_path = make_note_path(new_name, note.title);
            std::filesystem::create_directories(new_path.parent_path());
            moves.emplace_back(note.path, new_path);
          }
          if(!move_project_files(moves)) continue;
          for(std::size_t note_index = 0; note_index < mf.notes.size(); ++note_index)
          {
            mf.notes[note_index].path = moves[note_index].second.string();
            remove_pending_delete_path(mf.notes[note_index].path);
          }
          mf.name = new_name;
        }

        std::vector<std::pair<std::string, std::vector<FreeStroke>>> drawings_to_reinsert;
        for(const auto &kv : g_folder_drawings)
        {
          const std::string &k = kv.first;
          if(k == src_name || StringUtils::starts_with(k, src_name + "/"))
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
  if(pending_move_image_src_fi >= 0 && pending_move_image_dst_fi >= 0 &&
     !pending_move_image_path.empty())
  {
    const int src_fi = pending_move_image_src_fi;
    const int dst_fi = pending_move_image_dst_fi;
    if(src_fi >= 0 && src_fi < (int)folders_.size() &&
       dst_fi >= 0 && dst_fi < (int)folders_.size() &&
       src_fi != dst_fi)
    {
      FolderMeta &src_f = folders_[(size_t)src_fi];
      FolderMeta &dst_f = folders_[(size_t)dst_fi];
      const std::filesystem::path src_path(pending_move_image_path);
      const std::filesystem::path dst_dir = (dst_f.name == ".")
                                                ? config_.dataPath
                                                : (config_.dataPath / dst_f.name);
      std::error_code create_error;
      std::filesystem::create_directories(dst_dir, create_error);
      const std::filesystem::path dst_path = dst_dir / src_path.filename();
      if(!create_error && move_project_file(src_path, dst_path))
      {
        src_f.images.erase(
            std::remove(src_f.images.begin(), src_f.images.end(), pending_move_image_path),
            src_f.images.end());
        dst_f.images.push_back(dst_path.string());
        invalidate_folder_image_cache(src_f.name);
        invalidate_folder_image_cache(dst_f.name);
        save_index();
      }
    }
    pending_move_image_src_fi = -1;
    pending_move_image_dst_fi = -1;
    pending_move_image_path.clear();
  }
  force_open_folder_idx = -1;
  if(request_rename_selected_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
  {
    const int fi = active_folder_idx_;
    if(!sidebar_last_selected_was_folder)
    {
      const int ni = active_note_idx_;
      if(fi >= 0 && fi < (int)folders_.size() &&
         ni >= 0 && ni < (int)folders_[(size_t)fi].notes.size())
      {
        rename_note_folder_idx = fi;
        rename_note_idx = ni;
        std::snprintf(rename_note_buf, sizeof(rename_note_buf), "%s",
                      folders_[(size_t)fi].notes[(size_t)ni].title.c_str());
        open_rename_note_popup = true;
      }
    }
    else
    {
      if(fi >= 0 && fi < (int)folders_.size())
      {
        rename_folder_idx = fi;
        std::snprintf(rename_folder_buf, sizeof(rename_folder_buf), "%s",
                      folder_base_name(folders_[(size_t)fi].name).c_str());
        open_rename_folder_popup = true;
      }
    }
    request_rename_selected_ = false;
  }
  ImGui::PopStyleColor(4);
  ImGui::End();

  auto read_file_text = [this](const std::string &path) -> const std::string & {
    return cached_note_text(path);
  };

  if(folder_overview_mode_)
  {
    static bool refocus_folder_editor = false;
    if(request_focus_editor_)
    {
      refocus_folder_editor = true;
      request_focus_editor_ = false;
    }
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
    bool &drawings_visible = drawings_visible_;
    bool &grid_visible = grid_visible_;
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
    static bool note_drag_snapshot_taken = false;
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
    const std::string preview_state_before_frame = capture_preview_state_snapshot();
    const ImVec4 neutral_sel(0.26f, 0.59f, 0.98f, 1.0f);
    for(auto it = selected_note_indices.begin(); it != selected_note_indices.end();)
    {
      const int idx = *it;
      if(idx < 0 || idx >= (int)f.notes.size() || f.notes[(size_t)idx].hidden)
        it = selected_note_indices.erase(it);
      else
        ++it;
    }
    bool request_reset_layout = false;
    auto reset_note_positions = [&]() {
      const float top_bar_h = 32.0f;
      const float canvas_x = vp->Pos.x + explorer_w + 14.0f;
      const float canvas_y = vp->Pos.y + top_bar_h + 14.0f;
      const float canvas_w = std::max(280.0f, vp->Size.x - explorer_w - 28.0f);
      const float base_w = std::max(340.0f, std::min(520.0f, canvas_w * 0.45f));
      const float gap = 14.0f;
      const int cols = std::max(1, (int)((canvas_w + gap) / (base_w + gap)));

      for(int i = 0; i < (int)f.notes.size(); ++i)
      {
        NoteMeta &n = f.notes[(size_t)i];
        const int col = i % cols;
        const int row = i / cols;
        n.hidden = false;
        n.has_layout = true;
        n.width = base_w;
        n.height = std::max(170.0f, n.height);
        n.pos_x = canvas_x + (base_w + gap) * (float)col;
        n.pos_y = canvas_y + (n.height + gap) * (float)row;
      }
      request_reset_layout = true;
      layout_dirty_ = true;
      save_index();
    };
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
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) note_drag_snapshot_taken = false;
    static std::string deferred_draw_snapshot_before;
    static std::string deferred_selection_snapshot_before;
    auto push_draw_snapshot = [&](const std::string &folder_key) {
      (void)folder_key;
      if(deferred_draw_snapshot_before.empty()) deferred_draw_snapshot_before = capture_workspace_before();
    };
    auto apply_draw_undo = [&](const std::string &folder_key) {
      (void)folder_key;
      return apply_global_undo();
    };
    auto apply_draw_redo = [&](const std::string &folder_key) {
      (void)folder_key;
      return apply_global_redo();
    };
    auto push_selection_snapshot = [&]() {
      if(deferred_selection_snapshot_before.empty()) deferred_selection_snapshot_before = capture_workspace_before();
    };
    auto apply_selection_undo = [&]() -> bool { return apply_global_undo(); };
    auto apply_selection_redo = [&]() -> bool { return apply_global_redo(); };

    {
      const bool has_anchor_selection = (anchor_sel_start != anchor_sel_end);
      const float bar_h = 32.0f;
      ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + explorer_w, vp->Pos.y), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(std::max(200.0f, vp->Size.x - explorer_w), bar_h), ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
      const ImVec4 topbar_bg = explorer_bg;
      ImGui::PushStyleColor(ImGuiCol_WindowBg, topbar_bg);
      ImGui::Begin(
          "##FormatTopBar",
          nullptr,
          ImGuiWindowFlags_NoTitleBar |
              ImGuiWindowFlags_NoMove |
              ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoSavedSettings |
              ImGuiWindowFlags_NoDocking |
              (dock_drag_active ? ImGuiWindowFlags_NoInputs : 0));
      ImGui::PopStyleColor();
      const ImVec4 top_frame(0.18f, 0.24f, 0.34f, 0.92f);
      const ImVec4 top_frame_hov(0.22f, 0.34f, 0.52f, 0.96f);
      const ImVec4 top_frame_act(0.12f, 0.42f, 0.74f, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.24f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, top_frame);
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, top_frame_hov);
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, top_frame_act);
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.32f, 0.36f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.94f, 0.96f, 1.0f));

      auto insert_topbar_snippet = [&](std::string snippet, bool wrap_in_ui_fences = false) {
        static constexpr const char *kCursorMarker = "__CURSOR__";
        int cursor_offset = static_cast<int>(snippet.size());
        const size_t marker_pos = snippet.find(kCursorMarker);
        if(marker_pos != std::string::npos)
        {
          cursor_offset = static_cast<int>(marker_pos);
          snippet.erase(marker_pos, std::strlen(kCursorMarker));
        }
        const int cursor_pos = std::max(0, std::min(fmt_folder.cursor_pos, static_cast<int>(markdown_text_.size())));
        if(wrap_in_ui_fences && !cursor_is_inside_ui_block(markdown_text_, cursor_pos))
        {
          std::string wrapped = "```ui\n";
          cursor_offset += static_cast<int>(wrapped.size());
          wrapped += snippet;
          if(!snippet.empty() && snippet.back() != '\n') wrapped += '\n';
          wrapped += "```\n";
          snippet = std::move(wrapped);
        }
        int p = cursor_pos;
        if(p > 0 && !snippet.empty() && markdown_text_[static_cast<size_t>(p) - 1] != '\n' && snippet.front() != '\n')
        {
          snippet.insert(snippet.begin(), '\n');
          ++cursor_offset;
        }
        push_undo_snapshot();
        markdown_text_.insert(static_cast<size_t>(p), snippet);
        const int cursor = p + std::max(0, cursor_offset);
        fmt_folder.cursor_pos = cursor;
        fmt_folder.sel_start = cursor;
        fmt_folder.sel_end = cursor;
        fmt_folder.selection_anchor = cursor;
        fmt_folder.pending_select_range = true;
        fmt_folder.pending_sel_start = cursor;
        fmt_folder.pending_sel_end = cursor;
        refocus_folder_editor = true;
        normalize_input_text_buffer(markdown_text_);
        state_dirty_ = true;
      };

      static int table_builder_rows = 2;
      static int table_builder_cols = 2;

      constexpr float kIconH = 22.0f;
      auto icon_sz = [](const char *name) -> ImVec2 {
        const ImVec2 orig = get_toolbar_icon_size(name);
        if(orig.y <= 0.0f) return ImVec2(kIconH, kIconH);
        return ImVec2(orig.x * (kIconH / orig.y), kIconH);
      };

      if(editing_mode_)
      {
        const ImTextureID ic_italic = get_toolbar_icon_texture("italic.png");
        const ImTextureID ic_bold = get_toolbar_icon_texture("bold.png");
        const ImTextureID ic_strike = get_toolbar_icon_texture("strike.png");
        const ImTextureID ic_note = get_toolbar_icon_texture("note.png");
        const ImTextureID ic_color = get_toolbar_icon_texture("color-brush.png");
        const ImTextureID ic_task = get_toolbar_icon_texture("to-do-list.png");
        const ImTextureID ic_table = get_toolbar_icon_texture("table.png");
        const ImTextureID ic_widget = get_toolbar_icon_texture("widgets.png");
        const ImTextureID ic_find = get_toolbar_icon_texture("find.png");
        const ImVec2 sz_italic = icon_sz("italic.png");
        const ImVec2 sz_bold = icon_sz("bold.png");
        const ImVec2 sz_strike = icon_sz("strike.png");
        const ImVec2 sz_note = icon_sz("note.png");
        const ImVec2 sz_color = icon_sz("color-brush.png");
        const ImVec2 sz_task = icon_sz("to-do-list.png");
        const ImVec2 sz_table = icon_sz("table.png");
        const ImVec2 sz_widget = icon_sz("widgets.png");
        const ImVec2 sz_find = icon_sz("find.png");
        auto tool_button = [&](const char *id, ImTextureID tex, ImVec2 disp_sz, const char *fallback, const char *tooltip) -> bool {
          const bool pressed = shaded_icon_button(id, tex, disp_sz, fallback);
          if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) topbar_tooltip_text = tooltip;
          return pressed;
        };
        if(request_editor_action_ != EditorAction::none)
        {
          const bool selection_action =
              request_editor_action_ == EditorAction::bold ||
              request_editor_action_ == EditorAction::italic ||
              request_editor_action_ == EditorAction::strikethrough ||
              request_editor_action_ == EditorAction::quote ||
              request_editor_action_ == EditorAction::color;
          if(!selection_action || has_anchor_selection)
          {
            switch(request_editor_action_)
            {
            case EditorAction::task_list:
              push_undo_snapshot();
              insert_checklist_item_at_cursor(markdown_text_, fmt_folder);
              fmt_folder.sel_start = fmt_folder.cursor_pos;
              fmt_folder.sel_end = fmt_folder.cursor_pos;
              fmt_folder.selection_anchor = fmt_folder.cursor_pos;
              fmt_folder.pending_select_range = true;
              fmt_folder.pending_sel_start = fmt_folder.cursor_pos;
              fmt_folder.pending_sel_end = fmt_folder.cursor_pos;
              break;
            case EditorAction::table:
              ImGui::OpenPopup("##tb_table_builder_popup");
              break;
            case EditorAction::bold:
              push_undo_snapshot();
              apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
              break;
            case EditorAction::italic:
              push_undo_snapshot();
              apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
              break;
            case EditorAction::strikethrough:
              push_undo_snapshot();
              apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
              break;
            case EditorAction::quote:
              push_undo_snapshot();
              apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
              break;
            case EditorAction::color:
              push_undo_snapshot();
              apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, rgba_to_hex(fmt_folder.color));
              break;
            case EditorAction::ui_block:
              editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::open_requested;
              break;
            case EditorAction::none:
              break;
            }
            if(selection_action)
            {
              fmt_folder.sel_start = anchor_sel_start;
              fmt_folder.sel_end = anchor_sel_end;
              fmt_folder.cursor_pos = anchor_sel_end;
              fmt_folder.selection_anchor = anchor_sel_start;
              fmt_folder.pending_select_range = true;
              fmt_folder.pending_sel_start = anchor_sel_start;
              fmt_folder.pending_sel_end = anchor_sel_end;
            }
            if(request_editor_action_ != EditorAction::table &&
               request_editor_action_ != EditorAction::ui_block)
            {
              normalize_input_text_buffer(markdown_text_);
              state_dirty_ = true;
              refocus_folder_editor = true;
            }
          }
          request_editor_action_ = EditorAction::none;
        }
        ImGui::BeginDisabled(!has_anchor_selection);
        if(tool_button("##tb_italic", ic_italic, sz_italic, "Italic", Lang::t("Italic")))
        {
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          fmt_folder.sel_start = anchor_sel_start;
          fmt_folder.sel_end = anchor_sel_end;
          fmt_folder.cursor_pos = anchor_sel_end;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = anchor_sel_start;
          fmt_folder.pending_sel_end = anchor_sel_end;
          refocus_folder_editor = true;
        }
        ImGui::SameLine();
        if(tool_button("##tb_bold", ic_bold, sz_bold, "Bold", Lang::t("Bold")))
        {
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          fmt_folder.sel_start = anchor_sel_start;
          fmt_folder.sel_end = anchor_sel_end;
          fmt_folder.cursor_pos = anchor_sel_end;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = anchor_sel_start;
          fmt_folder.pending_sel_end = anchor_sel_end;
          refocus_folder_editor = true;
        }
        ImGui::SameLine();
        if(tool_button("##tb_strike", ic_strike, sz_strike, "Strike", Lang::t("Strike")))
        {
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          fmt_folder.sel_start = anchor_sel_start;
          fmt_folder.sel_end = anchor_sel_end;
          fmt_folder.cursor_pos = anchor_sel_end;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = anchor_sel_start;
          fmt_folder.pending_sel_end = anchor_sel_end;
          refocus_folder_editor = true;
        }
        ImGui::SameLine();
        if(tool_button("##tb_note", ic_note, sz_note, "Note", Lang::t("Note quote")))
        {
          push_undo_snapshot();
          apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          fmt_folder.sel_start = anchor_sel_start;
          fmt_folder.sel_end = anchor_sel_end;
          fmt_folder.cursor_pos = anchor_sel_end;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = anchor_sel_start;
          fmt_folder.pending_sel_end = anchor_sel_end;
          refocus_folder_editor = true;
        }
        ImGui::SameLine();
        ImGui::ColorEdit3("##top_color", (float *)&fmt_folder.color, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if(tool_button("##tb_color_apply", ic_color, sz_color, "Color", Lang::t("Apply color")))
        {
          push_undo_snapshot();
          const std::string hex = rgba_to_hex(fmt_folder.color);
          apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, hex);
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          fmt_folder.sel_start = anchor_sel_start;
          fmt_folder.sel_end = anchor_sel_end;
          fmt_folder.cursor_pos = anchor_sel_end;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = anchor_sel_start;
          fmt_folder.pending_sel_end = anchor_sel_end;
          refocus_folder_editor = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(tool_button("##tb_task", ic_task, sz_task, "Task", Lang::t("Task list")))
        {
          push_undo_snapshot();
          insert_checklist_item_at_cursor(markdown_text_, fmt_folder);
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = fmt_folder.cursor_pos;
          fmt_folder.pending_sel_end = fmt_folder.cursor_pos;
          refocus_folder_editor = true;
        }
        ImGui::SameLine();
        if(tool_button("##tb_table", ic_table, sz_table, "Table", Lang::t("Insert markdown table")))
        {
          ImGui::OpenPopup("##tb_table_builder_popup");
        }
        ImGui::SameLine();
        if(tool_button("##tb_ui_widgets", ic_widget, sz_widget, "Widgets", Lang::t("Insert UI widget")))
        {
          ImGui::OpenPopup(kToolbarUiWidgetChooserPopup);
        }
        const bool render_editor_action_ui_widget_chooser =
            editor_action_ui_widget_chooser_state_ != UiWidgetChooserState::closed;
        if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open_requested)
        {
          ImGui::SetNextWindowFocus();
          ImGui::OpenPopup(kEditorActionsUiWidgetChooserPopup);
        }
        const char *const ui_widget_chooser_popup = render_editor_action_ui_widget_chooser
                                                        ? kEditorActionsUiWidgetChooserPopup
                                                        : kToolbarUiWidgetChooserPopup;
        const bool ui_widget_chooser_open = render_ui_widget_chooser(
            ui_widget_chooser_popup,
            render_editor_action_ui_widget_chooser,
            request_close_editor_action_ui_widget_chooser_,
            insert_topbar_snippet,
            [&]() {
              if(active_folder_idx_ < 0 || active_folder_idx_ >= (int)folders_.size()) return;
              perform_workspace_change("Create .globals.md", [&]() {
                FolderMeta &gf = folders_[(size_t)active_folder_idx_];
                const std::string globals_title = ".globals";
                const std::string globals_path = make_note_path(gf.name, globals_title);
                const auto found = std::find_if(
                    gf.notes.begin(), gf.notes.end(),
                    [&](const NoteMeta &note) { return note.path == globals_path; });
                if(found == gf.notes.end())
                {
                  NoteMeta globals;
                  globals.id = generate_uuid();
                  globals.title = globals_title;
                  globals.path = globals_path;
                  if(!std::filesystem::exists(globals_path))
                    write_text_file(globals_path,
                                    "```ui\ncampaign(\"My Campaign\")\nparty_level(1)\ngold(0)\n```\n");
                  gf.notes.push_back(std::move(globals));
                  flash_mark_note(gf.notes.back().path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
                  flash_mark_folder(gf.name, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
                  active_note_idx_ = static_cast<int>(gf.notes.size()) - 1;
                  load_note_content_for_active();
                  save_index();
                  return;
                }
                active_note_idx_ = static_cast<int>(std::distance(gf.notes.begin(), found));
                load_note_content_for_active();
                show_history_indicator("Opened", ".globals.md", ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
              });
            });
        if(request_close_editor_action_ui_widget_chooser_)
        {
          editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
          request_close_editor_action_ui_widget_chooser_ = false;
        }
        else if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open_requested &&
                ui_widget_chooser_open)
        {
          editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::open;
        }
        else if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open &&
                !ui_widget_chooser_open)
        {
          editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
        }
        if(ImGui::BeginPopupModal("##tb_table_builder_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
          ImGui::TextDisabled("%s", Lang::t("Insert Markdown Table"));
          ImGui::SetNextItemWidth(120.0f);
          ImGui::InputInt(Lang::t("Rows"), &table_builder_rows);
          ImGui::SetNextItemWidth(120.0f);
          ImGui::InputInt(Lang::t("Cols"), &table_builder_cols);
          table_builder_rows = std::max(1, table_builder_rows);
          table_builder_cols = std::max(1, table_builder_cols);
          ImGui::TextDisabled("Cells: %d", table_builder_rows * table_builder_cols);
          if(ImGui::Button(Lang::t("Insert")))
          {
            push_undo_snapshot();
            insert_markdown_table_at_cursor(markdown_text_, fmt_folder, table_builder_rows, table_builder_cols);
            normalize_input_text_buffer(markdown_text_);
            state_dirty_ = true;
            fmt_folder.pending_select_range = true;
            fmt_folder.pending_sel_start = fmt_folder.sel_start;
            fmt_folder.pending_sel_end = fmt_folder.sel_end;
            refocus_folder_editor = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if(ImGui::Button(Lang::t("Cancel")))
          {
            refocus_folder_editor = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }
        ImGui::SameLine();
        if(tool_button("##tb_find", ic_find, sz_find, "Find", Lang::t("Find (Ctrl+F)")))
        {
          request_open_search_ = true;
        }
#if !defined(_WIN32)
        ImGui::SameLine();
        if(tool_button("##tb_emoji", (ImTextureID)0, ImVec2(kIconH, kIconH), "😀", Lang::t("Emoji picker (Ctrl+.)")))
        {
          emoji_picker_.reset_search();
          ImGui::OpenPopup("##emoji_picker_modal");
        }
        if(show_emoji_picker_)
        {
          emoji_picker_.reset_search();
          ImGui::OpenPopup("##emoji_picker_modal");
          show_emoji_picker_ = false;
        }
        ImGui::SetNextWindowSize({300.0f, 370.0f}, ImGuiCond_Always);
        static bool emoji_picker_was_open = false;
        if(ImGui::BeginPopupModal("##emoji_picker_modal", nullptr,
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
        {
          emoji_picker_was_open = true;
          if(emoji_picker_.render_content())
          {
            const int pos = std::max(0, std::min(fmt_folder.cursor_pos, static_cast<int>(markdown_text_.size())));
            push_undo_snapshot();
            markdown_text_.insert(static_cast<size_t>(pos), emoji_picker_.last_selected);
            normalize_input_text_buffer(markdown_text_);
            state_dirty_ = true;
            const int new_pos = pos + static_cast<int>(emoji_picker_.last_selected.size());
            fmt_folder.cursor_pos = new_pos;
            fmt_folder.sel_start = new_pos;
            fmt_folder.sel_end = new_pos;
            fmt_folder.selection_anchor = new_pos;
            fmt_folder.pending_select_range = true;
            fmt_folder.pending_sel_start = new_pos;
            fmt_folder.pending_sel_end = new_pos;
          }
          ImGui::EndPopup();
        }
        else if(emoji_picker_was_open)
        {
          emoji_picker_was_open = false;
          refocus_folder_editor = true;
        }
#endif
        ImGui::SameLine();
      }

      const auto render_terminal_button = [&]() {
        const ImTextureID terminal_icon = get_toolbar_icon_texture("terminal.png");
        const ImVec2 terminal_sz = icon_sz("terminal.png");
        if(shaded_icon_button("##terminal_btn", terminal_icon, terminal_sz, ">_", terminal_visible_))
          request_open_terminal_ = true;
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s", Lang::t("Terminal"));
      };

      if(!editing_mode_)
      {
        const ImTextureID mouse_icon = get_toolbar_icon_texture("cursor.png");
        const ImTextureID draw_icon = get_toolbar_icon_texture("pencil.png");
        const ImTextureID erase_icon = get_toolbar_icon_texture("erase.png");
        const ImTextureID clear_icon = get_toolbar_icon_texture("delete-bin.png");
        const ImTextureID reset_icon = get_toolbar_icon_texture("focus.png");
        const ImTextureID lock_icon = get_toolbar_icon_texture(layout_locked_ ? "unlock.png" : "lock.png");
        const ImTextureID detach_icon = get_toolbar_icon_texture("detach.png");
        const ImTextureID show_icon = get_toolbar_icon_texture(drawings_visible ? "hide.png" : "show.png");
        const ImTextureID grid_icon = get_toolbar_icon_texture(grid_visible ? "hide-grid.png" : "show-grid.png");
        const ImVec2 sz_mouse = icon_sz("cursor.png");
        const ImVec2 sz_draw = icon_sz("pencil.png");
        const ImVec2 sz_erase = icon_sz("erase.png");
        const ImVec2 sz_clear = icon_sz("delete-bin.png");
        const ImVec2 sz_reset = icon_sz("focus.png");
        const ImVec2 sz_lock = icon_sz(layout_locked_ ? "unlock.png" : "lock.png");
        const ImVec2 sz_detach = icon_sz("detach.png");
        const ImVec2 sz_show = icon_sz(drawings_visible ? "hide.png" : "show.png");
        const ImVec2 sz_grid = icon_sz(grid_visible ? "hide-grid.png" : "show-grid.png");
        auto mode_button = [&](const char *id, ImTextureID icon, ImVec2 disp_sz, const char *fallback, const char *tooltip, bool active) -> bool {
          const bool pressed = shaded_icon_button(id, icon, disp_sz, fallback, active);
          if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            topbar_tooltip_text = tooltip;
          return pressed;
        };

        const bool mouse_mode = !draw_mode && !erase_mode;
        if(mode_button("##mode_mouse_icon", mouse_icon, sz_mouse, Lang::t("Mouse"), Lang::t("Mouse"), mouse_mode))
        {
          draw_mode = false;
          erase_mode = false;
          stroke_in_progress = false;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!drawings_visible);
        const bool draw_button_pressed = mode_button(
            "##mode_draw_icon",
            draw_icon,
            sz_draw,
            Lang::t("Draw"),
            Lang::t("Draw (right click for color)"),
            draw_mode);
        const bool draw_button_right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if(draw_button_pressed)
        {
          const bool was_draw_mode = draw_mode;
          draw_mode = true;
          erase_mode = false;
          stroke_in_progress = false;
          if(was_draw_mode) ImGui::OpenPopup("##draw_color_popup");
        }
        if(draw_button_right_clicked) ImGui::OpenPopup("##draw_color_popup");

        if(ImGui::BeginPopup("##draw_color_popup"))
        {
          ImGui::TextDisabled("%s", Lang::t("Draw color"));
          ImGui::Separator();
          ImGui::ColorEdit3("##draw_color_popup_value", (float *)&draw_color, ImGuiColorEditFlags_NoInputs);
          const ImVec4 presets[] = {
              ImVec4(1.00f, 0.30f, 0.10f, 1.0f),
              ImVec4(0.96f, 0.72f, 0.15f, 1.0f),
              ImVec4(0.30f, 0.83f, 0.56f, 1.0f),
              ImVec4(0.22f, 0.62f, 0.95f, 1.0f),
              ImVec4(0.74f, 0.46f, 0.96f, 1.0f),
              ImVec4(0.98f, 0.98f, 0.98f, 1.0f)};
          for(int ci = 0; ci < (int)(sizeof(presets) / sizeof(presets[0])); ++ci)
          {
            if(ci != 0) ImGui::SameLine();
            ImGui::PushID(ci);
            if(ImGui::ColorButton("##draw_color_preset", presets[ci], ImGuiColorEditFlags_NoTooltip, ImVec2(16.0f, 16.0f)))
            {
              draw_color = presets[ci];
            }
            ImGui::PopID();
          }
          ImGui::EndPopup();
        }

        ImGui::SameLine();
        if(mode_button("##mode_erase_icon", erase_icon, sz_erase, Lang::t("Erase"), Lang::t("Erase"), erase_mode))
        {
          erase_mode = true;
          draw_mode = false;
          stroke_in_progress = false;
        }
        ImGui::SameLine();
        if(shaded_icon_button("##clear_draw_icon", clear_icon, sz_clear, Lang::t("Clear drawing")))
        {
          push_draw_snapshot(f.name);
          g_folder_drawings[f.name].clear();
          stroke_in_progress = false;
          g_drawings_dirty = true;
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) topbar_tooltip_text = Lang::t("Clear drawing");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(shaded_icon_button("##toggle_draw_visibility_icon", show_icon, sz_show,
                              drawings_visible ? Lang::t("Hide") : Lang::t("Show")))
        {
          drawings_visible = !drawings_visible;
          stroke_in_progress = false;
          if(!drawings_visible)
          {
            draw_mode = false;
            erase_mode = false;
            selected_stroke_indices.clear();
          }
          save_index();
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          topbar_tooltip_text = drawings_visible ? Lang::t("Hide drawings") : Lang::t("Show drawings");
        ImGui::SameLine();
        if(shaded_icon_button("##toggle_grid_visibility_icon", grid_icon, sz_grid,
                              grid_visible ? Lang::t("Hide grid") : Lang::t("Show grid")))
        {
          grid_visible = !grid_visible;
          save_index();
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          topbar_tooltip_text = grid_visible ? Lang::t("Hide grid") : Lang::t("Show grid");
        ImGui::SameLine();
        {
          const float icon_slot_w = 16.0f + ImGui::GetStyle().ItemSpacing.x;
          ImGui::Dummy(ImVec2(icon_slot_w * 1.0f, 16.0f));
        }
        ImGui::SameLine();
        if(shaded_icon_button("##lock_layout_icon", lock_icon, sz_lock,
                              layout_locked_ ? Lang::t("Unlock note moving") : Lang::t("Lock note moving")))
        {
          push_sidebar_snapshot();
          layout_locked_ = !layout_locked_;
          save_index();
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          topbar_tooltip_text = layout_locked_ ? Lang::t("Unlock note moving") : Lang::t("Lock note moving");
        ImGui::SameLine();
        if(shaded_icon_button("##reset_positions_icon", reset_icon, sz_reset,
                              Lang::t("Reset positions")) &&
           !f.notes.empty())
        {
          push_selection_snapshot();
          reset_note_positions();
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) topbar_tooltip_text = Lang::t("Reset positions");
        ImGui::SameLine();

        if(mode_button(
               "##toggle_detach_note_windows_icon",
               detach_icon,
               sz_detach,
               Lang::t("Detach"),
               detached_note_windows_enabled_
                   ? Lang::t("Detach_enabled")
                   : Lang::t("Detach_disabled"),
               detached_note_windows_enabled_))
        {
          push_sidebar_snapshot();
          detached_note_windows_enabled_ = !detached_note_windows_enabled_;
          set_detached_note_windows_enabled(detached_note_windows_enabled_);
          save_index();
        }

        ImGui::SameLine(0.0f, 4.0f);
        {
          const ImVec2 dot_pos = ImGui::GetCursorScreenPos();
          const ImVec2 dot_center(dot_pos.x - 9.0f, dot_pos.y + 6.0f);
          ImDrawList *dl = ImGui::GetWindowDrawList();
          const ImU32 fill_col = detached_note_windows_enabled_
                                     ? ImGui::GetColorU32(ImVec4(0.30f, 0.83f, 0.56f, 1.0f))
                                     : ImGui::GetColorU32(ImVec4(0.15f, 0.18f, 0.22f, 1.0f));
          dl->AddCircleFilled(dot_center, 3.0f, fill_col, 16);
          ImGui::Dummy(ImVec2(10.0f, 16.0f));
        }
        ImGui::SameLine();

        {
          const ImTextureID dockers_icon = get_toolbar_icon_texture("docker.png");
          const ImVec2 sz_dockers = icon_sz("docker.png");
          if(mode_button(
                 "##toggle_dockers_icon",
                 dockers_icon,
                 sz_dockers,
                 Lang::t("Dockers"),
                 dockers_enabled_
                     ? Lang::t("Dockers_enabled")
                     : Lang::t("Dockers_disabled"),
                 dockers_enabled_))
          {
            push_sidebar_snapshot();
            dockers_enabled_ = !dockers_enabled_;
            set_dockers_enabled(dockers_enabled_);
            save_index();
          }

          ImGui::SameLine(0.0f, 4.0f);
          {
            const ImVec2 dot_pos = ImGui::GetCursorScreenPos();
            const ImVec2 dot_center(dot_pos.x - 9.0f, dot_pos.y + 6.0f);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            const ImU32 fill_col = dockers_enabled_
                                       ? ImGui::GetColorU32(ImVec4(0.30f, 0.83f, 0.56f, 1.0f))
                                       : ImGui::GetColorU32(ImVec4(0.15f, 0.18f, 0.22f, 1.0f));
            dl->AddCircleFilled(dot_center, 3.0f, fill_col, 16);
            ImGui::Dummy(ImVec2(10.0f, 16.0f));
          }
          ImGui::SameLine();
        }

        render_terminal_button();
      }

      if(editing_mode_) render_terminal_button();

      const float left_toolbar_right = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

      {
        const char *ver = "v" NOTEPP_VERSION;
        const ImVec2 ver_sz = ImGui::CalcTextSize(ver);
        const float right_margin = ImGui::GetStyle().WindowPadding.x;

        // Language selector button (left of version)
        const Lang::LanguageInfo *lang_info = Lang::current_language();
        const char *lang_flag_file = (lang_info && !lang_info->flag_icon.empty()) ? lang_info->flag_icon.c_str() : "";
        const char *lang_short = (lang_info && !lang_info->short_code.empty()) ? lang_info->short_code.c_str() : "EN";
        const ImTextureID lang_flag_tex = (*lang_flag_file) ? get_toolbar_icon_texture(lang_flag_file) : static_cast<ImTextureID>(0);
        // Compute display size preserving the image aspect ratio at kIconH.
        const ImVec2 lang_flag_display = lang_flag_tex ? icon_sz(lang_flag_file) : ImVec2(0, 0);
        const float lang_btn_w = lang_flag_tex ? lang_flag_display.x : (ImGui::CalcTextSize(lang_short).x + 10.0f);
        const float lang_btn_h = lang_flag_tex ? lang_flag_display.y : 18.0f;
        // Custom window control buttons (quit / minimize) — rightmost
        const ImTextureID quit_icon = get_toolbar_icon_texture("quit.png");
        const ImTextureID min_icon = get_toolbar_icon_texture("minimize.png");
        const ImVec2 quit_sz = icon_sz("quit.png");
        const ImVec2 min_sz = icon_sz("minimize.png");
        const float ctrl_y = (bar_h - kIconH) * 0.5f;
        const float quit_x = ImGui::GetWindowWidth() - quit_sz.x - right_margin;
        const float min_x = quit_x - min_sz.x - 4.0f;

        // Quit button
        ImGui::SetCursorPos(ImVec2(quit_x, ctrl_y));
        if(shaded_icon_button("##quit_btn", quit_icon, quit_sz, "✕"))
          request_close();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s", Lang::t("Quit"));

        // Minimize button
        ImGui::SetCursorPos(ImVec2(min_x, ctrl_y));
        if(shaded_icon_button("##min_btn", min_icon, min_sz, "─"))
          SDL_MinimizeWindow(window_);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s", Lang::t("Minimize"));

        // Language selector and layout profiles
        constexpr float gap = 6.0f;
        constexpr float profile_combo_w = 120.0f;
        constexpr float profile_controls_gap = 2.0f;
        const float lang_x = min_x - ver_sz.x - gap - lang_btn_w - gap;
        const float profile_button_w = ImGui::CalcTextSize(Lang::t("Profiles...")).x +
                                       ImGui::GetStyle().FramePadding.x * 2.0f;
        const float profile_group_w = profile_combo_w + profile_controls_gap + profile_button_w;
        const float profile_x = lang_x - gap - profile_group_w;

        // Keep the essential right-side controls visible when the toolbar is too narrow.
        if(profile_x >= left_toolbar_right + gap)
        {
          ImGui::SetCursorPos(ImVec2(profile_x, (bar_h - ImGui::GetFrameHeight()) * 0.5f));
          // Layout profiles — compact selector + manage button
          {
            // Build list of visible (non-deleted) profiles
            std::vector<int> visible_pidx;
            for(int pi = 0; pi < (int)layout_profiles_.size(); ++pi)
              if(!layout_profiles_[(size_t)pi].pending_delete)
                visible_pidx.push_back(pi);

            const LayoutProfile *active_p = find_active_profile();
            const char *combo_label = active_p ? active_p->name.c_str() : Lang::t("(Custom)");
            ImGui::PushItemWidth(profile_combo_w);
            if(ImGui::BeginCombo("##layout_profile_combo", combo_label, ImGuiComboFlags_HeightSmall))
            {
              for(int pi : visible_pidx)
              {
                const auto &p = layout_profiles_[(size_t)pi];
                const bool selected = (p.id == active_profile_id_);
                ImGui::PushID(pi);
                if(ImGui::Selectable(p.name.c_str(), selected) && !selected)
                {
                  capture_to_active_profile();
                  active_profile_id_ = p.id;
                  apply_profile(layout_profiles_[(size_t)pi], true);
                  static constexpr int kProfileSwitchSettle = 20;
                  window_profile_check_pending_ = false;
                  window_profile_check_delay_ = kProfileSwitchSettle;
                  save_profiles();
                  save_index();
                  if(editing_mode_) refocus_folder_editor = true;
                }
                ImGui::PopID();
              }
              ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
              topbar_tooltip_text = Lang::t("Layout profile");

            ImGui::SameLine(0.0f, 2.0f);
            if(ImGui::SmallButton(Lang::t("Profiles...")))
              manage_profiles_open_ = true;
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
              topbar_tooltip_text = Lang::t("Manage profiles");
            ImGui::SameLine();

            // ---- Profile management popup ----
            if(manage_profiles_open_)
              ImGui::OpenPopup("##manage_profiles_popup");

            static bool profiles_popup_was_open = false;
            if(ImGui::BeginPopup("##manage_profiles_popup",
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
            {
              profiles_popup_was_open = true;
              manage_profiles_open_ = false; // reset — popup stays open via BeginPopup

              ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
              ImGui::TextUnformatted(Lang::t("Layout Profiles"));
              ImGui::PopStyleColor();
              ImGui::Separator();

              if(visible_pidx.empty())
              {
                ImGui::TextDisabled("%s", Lang::t("No profiles."));
              }
              for(int pi : visible_pidx)
              {
                auto &p = layout_profiles_[(size_t)pi];
                const bool is_active = (p.id == active_profile_id_);
                ImGui::PushID(pi);

                // Active indicator
                if(is_active)
                {
                  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.75f, 0.35f, 1.0f));
                  ImGui::TextUnformatted("○");
                  ImGui::PopStyleColor();
                }
                else
                {
                  ImGui::TextDisabled("○");
                }
                ImGui::SameLine();

                // Profile name (click to switch)
                ImGui::PushStyleColor(ImGuiCol_Text, is_active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.75f, 0.78f, 0.85f, 1.0f));
                if(ImGui::Selectable(p.name.c_str(), is_active,
                                     ImGuiSelectableFlags_None, ImVec2(140.0f, 0.0f)) &&
                   !is_active)
                {
                  ImGui::CloseCurrentPopup();
                  capture_to_active_profile();
                  active_profile_id_ = p.id;
                  apply_profile(layout_profiles_[(size_t)pi], true);
                  static constexpr int kSettle = 20;
                  window_profile_check_pending_ = false;
                  window_profile_check_delay_ = kSettle;
                  save_profiles();
                  save_index();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();

                // Edit
                if(ImGui::SmallButton(Lang::t("Edit")))
                {
                  ImGui::CloseCurrentPopup();
                  profile_modal_.open = true;
                  profile_modal_.first_frame = true;
                  profile_modal_.edit_idx = pi;
                  profile_modal_.copy_mode = false;
                }
                ImGui::SameLine();
                // Copy
                if(ImGui::SmallButton(Lang::t("Copy")))
                {
                  ImGui::CloseCurrentPopup();
                  profile_modal_.open = true;
                  profile_modal_.first_frame = true;
                  profile_modal_.edit_idx = pi;
                  profile_modal_.copy_mode = true;
                }
                ImGui::SameLine();
                // Delete
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
                if(ImGui::SmallButton(Lang::t("Delete")))
                {
                  push_profile_snapshot();
                  p.pending_delete = true;
                  if(active_profile_id_ == p.id) active_profile_id_.clear();
                  // If last visible profile was deleted, open create dialog
                  int remaining = 0;
                  for(const auto &q : layout_profiles_)
                    if(!q.pending_delete) ++remaining;
                  if(remaining == 0)
                  {
                    profile_modal_.open = true;
                    profile_modal_.first_frame = true;
                    profile_modal_.edit_idx = -1;
                    profile_modal_.copy_mode = false;
                    std::strncpy(profile_modal_.name_buf, "Default",
                                 sizeof(profile_modal_.name_buf) - 1);
                  }
                  save_profiles();
                  ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
              }

              ImGui::Separator();
              if(ImGui::Button(Lang::t("+ New Profile"), ImVec2(-1.0f, 0.0f)))
              {
                ImGui::CloseCurrentPopup();
                profile_modal_.open = true;
                profile_modal_.first_frame = true;
                profile_modal_.edit_idx = -1;
                profile_modal_.copy_mode = false;
              }
              ImGui::EndPopup();
            }
            else if(profiles_popup_was_open)
            {
              profiles_popup_was_open = false;
              if(editing_mode_ && !profile_modal_.open) refocus_folder_editor = true;
            }
          }
        }

        ImGui::SetCursorPos(ImVec2(lang_x, (bar_h - lang_btn_h) * 0.5f));
        bool lang_clicked = shaded_icon_button("##lang_btn", lang_flag_tex, ImVec2(lang_btn_w, lang_btn_h), lang_short);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s", Lang::t("Language"));
        if(lang_clicked) ImGui::OpenPopup("##lang_select");
        const ImVec2 lang_btn_br = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(lang_btn_br.x - 140.0f, lang_btn_br.y + 2.0f));
        static bool lang_popup_was_open = false;
        if(ImGui::BeginPopup("##lang_select"))
        {
          lang_popup_was_open = true;
          for(const auto &lang : Lang::languages())
          {
            const bool selected = (lang.code == Lang::current_language_code());
            const ImTextureID ftex = lang.flag_icon.empty() ? static_cast<ImTextureID>(0) : get_toolbar_icon_texture(lang.flag_icon);
            ImGui::PushID(lang.code.c_str());
            if(ftex)
            {
              ImGui::Image(ftex, icon_sz(lang.flag_icon.c_str()));
              ImGui::SameLine();
            }
            if(ImGui::MenuItem(lang.name.c_str(), nullptr, selected))
            {
              if(lang.code != Lang::current_language_code())
              {
                if(Lang::set_language(lang.code))
                {
                  project_language_explicit_ = true;
                  project_settings_dirty_ = true;
                }
              }
            }
            ImGui::PopID();
          }
          ImGui::EndPopup();
        }
        else if(lang_popup_was_open)
        {
          lang_popup_was_open = false;
          if(editing_mode_) refocus_folder_editor = true;
        }

        // Version label
        const float ver_x = lang_x + lang_btn_w + gap;
        ImGui::SetCursorPos(ImVec2(ver_x, (bar_h - ver_sz.y) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.7f, 1.0f));
        ImGui::TextUnformatted(ver);
        ImGui::PopStyleColor();

        // Toolbar drag for borderless window (click + drag on empty toolbar area)
        {
          const ImVec2 tb_pos = ImGui::GetWindowPos();
          const ImVec2 tb_size = ImGui::GetWindowSize();
          const ImVec2 mp = ImGui::GetIO().MousePos;
          const bool over_toolbar = (mp.x >= tb_pos.x && mp.x < tb_pos.x + tb_size.x &&
                                     mp.y >= tb_pos.y && mp.y < tb_pos.y + tb_size.y);
          if(over_toolbar && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered())
          {
            window_drag_was_maximized_ = is_window_covering_display();
            SDL_GetGlobalMouseState(&window_drag_start_mx_, &window_drag_start_my_);
            SDL_GetWindowPosition(window_, &window_drag_start_wx_, &window_drag_start_wy_);
            window_drag_active_ = true;
          }
        }
      }
      ImGui::PopStyleColor(8);
      show_profile_modal();
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
            ImGuiWindowFlags_NoInputs |
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
      if(!apply_selection_undo())
      {
        if(!apply_sidebar_undo()) apply_draw_undo(f.name);
      }
      request_undo_draw_ = false;
    }
    if(request_redo_draw_)
    {
      if(!apply_selection_redo())
      {
        if(!apply_sidebar_redo()) apply_draw_redo(f.name);
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

    if(drawings_visible &&
       (draw_mode || erase_mode) &&
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
    if(drawings_visible && !draw_mode && !erase_mode && !editing_mode_)
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
      if(!layout_locked_ && (std::fabs(d.x) > 0.001f || std::fabs(d.y) > 0.001f))
      {
        for(int idx : selected_note_indices)
        {
          if(idx < 0 || idx >= (int)f.notes.size()) continue;
          NoteMeta &sn = f.notes[(size_t)idx];
          if(sn.hidden) continue;
          sn.pos_x += d.x;
          sn.pos_y += d.y;
          const std::string sid = sn.title + "###FolderNote_" + sn.id;
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

    if(grid_visible)
    {
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      dl->PushClipRect(bg_p0, bg_p1, true);
      constexpr float grid_step = 40.0f;
      const ImU32 grid_col = ImGui::GetColorU32(ImVec4(0.55f, 0.57f, 0.62f, 0.25f));
      for(float x = bg_p0.x + std::fmod(bg_p0.x, grid_step); x < bg_p1.x; x += grid_step)
        dl->AddLine(ImVec2(x, bg_p0.y), ImVec2(x, bg_p1.y), grid_col);
      for(float y = bg_p0.y + std::fmod(bg_p0.y, grid_step); y < bg_p1.y; y += grid_step)
        dl->AddLine(ImVec2(bg_p0.x, y), ImVec2(bg_p1.x, y), grid_col);
      dl->PopClipRect();
    }

    if(drawings_visible)
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

    if(notes_bg_hovered &&
       !mouse_over_note_area &&
       !popup_open &&
       ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      ImGui::OpenPopup("##notes_bg_ctx");
    }
    if(ImGui::BeginPopup("##notes_bg_ctx"))
    {
      if(ImGui::MenuItem(Lang::t("New note")))
      {
        new_note_target_folder_idx = active_folder_idx_;
        open_new_note_popup = true;
      }
      if(ImGui::MenuItem(Lang::t("Paste note"), nullptr, false, g_has_copied_note))
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

    // Canvas background: accept image drop (not over a note) to create new note
    if(ImGui::IsDragDropActive())
    {
      const ImRect canvas_rect(bg_p0, bg_p1);
      if(ImGui::BeginDragDropTargetCustom(canvas_rect,
                                          ImGui::GetID("##canvas_img_drop")))
      {
        const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("NOTEPP_IMAGE_INSERT");
        if(payload && !mouse_over_note_area)
        {
          const char *img_path_raw = static_cast<const char *>(payload->Data);
          const std::filesystem::path img_path_fs(img_path_raw);
          const std::string img_alt = img_path_fs.stem().string();

          push_sidebar_snapshot();
          ensure_default_index();
          FolderMeta &cf = folders_[(size_t)active_folder_idx_];
          const std::string new_title =
              make_unique_note_title(active_folder_idx_, img_alt);
          NoteMeta new_note;
          new_note.id = generate_uuid();
          new_note.title = new_title;
          new_note.path = make_note_path(cf.name, new_title);
          std::string img_rel;
          try
          {
            img_rel = std::filesystem::relative(img_path_fs, std::filesystem::path(new_note.path).parent_path()).string();
          }
          catch(...)
          {
            img_rel = img_path_fs.filename().string();
          }
          const std::string content = "![" + img_alt + "](" + img_rel + ")\n";
          const ImVec2 drop_pos = ImGui::GetMousePos();
          new_note.pos_x = drop_pos.x - 60.0f;
          new_note.pos_y = drop_pos.y - 30.0f;
          new_note.width = 400.0f;
          new_note.height = 300.0f;
          new_note.has_layout = true;
          remove_pending_delete_path(new_note.path);
          write_text_file(new_note.path, content);
          cf.notes.push_back(new_note);
          active_note_idx_ = (int)cf.notes.size() - 1;
          selected_note_indices.clear();
          selected_note_indices.insert(active_note_idx_);
          flash_mark_note(new_note.path, ImVec4(0.25f, 0.80f, 0.42f, 1.0f));
          save_index();
        }
        ImGui::EndDragDropTarget();
      }
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
             Lang::t("Name"),
             rename_win_buf,
             sizeof(rename_win_buf),
             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
      {
        if(rename_win_folder_idx >= 0 && rename_win_note_idx >= 0)
        {
          perform_workspace_change("Rename note", [&]() {
            rename_note_by_index(rename_win_folder_idx, rename_win_note_idx, rename_win_buf);
          });
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
      const std::string window_id = n.title + "###FolderNote_" + n.id;

      const bool force_note_layout = request_reset_layout || force_note_layout_restore_;
      const ImGuiID ini_dock_id = saved_window_dock_id(window_id.c_str());
      const bool can_restore_dock = dockers_enabled_ && imgui_docking_enabled();
      if(can_restore_dock && force_note_layout_restore_ && n.dock_id == 0 && ini_dock_id != 0)
      {
        n.dock_id = ini_dock_id;
        layout_dirty_ = true;
      }
      const bool restore_saved_dock =
          can_restore_dock && !request_reset_layout && force_note_layout_restore_ && n.dock_id != 0;
      if(request_reset_layout)
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
      else if(restore_saved_dock)
        ImGui::SetNextWindowDockID(n.dock_id, ImGuiCond_Always);
      if(n.has_layout)
      {
        const ImGuiCond cond = force_note_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        if(!restore_saved_dock)
        {
          ImGui::SetNextWindowPos(ImVec2(n.pos_x, n.pos_y), cond);
          ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, n.width), std::max(140.0f, n.height)), cond);
        }
      }
      else
      {
        const ImVec2 base(340.0f + 40.0f * (float)(ni % 3), 180.0f + 40.0f * (float)(ni % 3));
        const ImGuiCond cond = request_reset_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(base, cond);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 260.0f), cond);
      }

      bool note_window_open = true;
      ImGuiWindowFlags note_flags = 0;
      if(draw_mode || erase_mode) note_flags |= ImGuiWindowFlags_NoInputs;
      if(layout_locked_) note_flags |= ImGuiWindowFlags_NoMove;
      if(!dockers_enabled_) note_flags |= ImGuiWindowFlags_NoDocking;
      if(search_request_window_focus_ && ni == active_note_idx_) ImGui::SetNextWindowFocus();
      if(ni == pending_focus_note_idx) ImGui::SetNextWindowFocus();
      if(refocus_folder_editor && editing_mode_ && ni == active_note_idx_) ImGui::SetNextWindowFocus();
      const int folder_theme_count =
          push_folder_imgui_theme(make_note_theme(n.use_custom_color, n.color_r, n.color_g, n.color_b, ImGui::GetStyle()), ImGui::GetStyle());
      const bool note_window_visible = ImGui::Begin(
          window_id.c_str(),
          &note_window_open,
          note_flags);
      if(search_request_window_focus_ && ni == active_note_idx_) search_request_window_focus_ = false;
      if(ni == pending_focus_note_idx) pending_focus_note_idx = -1;
      const ImGuiID current_dock_id = ImGui::GetWindowDockID();
      if((can_restore_dock || request_reset_layout) && n.dock_id != current_dock_id)
      {
        n.dock_id = current_dock_id;
        layout_dirty_ = true;
      }
      const bool is_editing_this = editing_mode_ && ni == active_note_idx_;

      // Per-note custom font / size
      ImFont *note_font = nullptr;
      {
        constexpr float kDefaultSize = 14.0f;
        const float effective_size = n.font_size > 0.0f ? n.font_size : kDefaultSize;
        std::string effective_path;
        if(!n.font_path.empty())
        {
          std::filesystem::path abs_font(n.font_path);
          if(abs_font.is_relative()) abs_font = config_.dataPath / f.name / abs_font;
          effective_path = abs_font.string();
        }
        else if(n.font_size > 0.0f && !default_font_path_.empty())
        {
          effective_path = default_font_path_;
        }
        if(!effective_path.empty())
        {
          note_font = get_or_load_note_font(effective_path, effective_size);
          if(note_font)
          {
            ImGui::PushFont(note_font);
            MarkdownView::set_fonts(note_font, font_italic_, font_bold_);
          }
          else
            note_font = nullptr;
        }
      }

      // Ctrl+scroll to change font size
      if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().KeyCtrl)
      {
        const float wheel = ImGui::GetIO().MouseWheel;
        if(wheel != 0.0f)
        {
          constexpr float kDefaultSize = 14.0f;
          constexpr float kMinSize = 8.0f;
          constexpr float kMaxSize = 48.0f;
          const float current = n.font_size > 0.0f ? n.font_size : kDefaultSize;
          const float next = std::max(kMinSize, std::min(kMaxSize, current + wheel));
          n.font_size = (std::fabs(next - kDefaultSize) < 0.5f) ? 0.0f : next;
          save_index();
        }
      }

      const ImVec2 win_pos = ImGui::GetWindowPos();
      const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
      const ImVec2 mouse_pos = ImGui::GetMousePos();
      const std::string actions_popup_id = "Note Window Actions##" + n.id;
      ImGuiViewport *note_viewport = ImGui::GetWindowViewport();
      const bool note_is_detached = viewport_is_detached_from_main(note_viewport, window_);
      const bool note_is_collapsed = ImGui::IsWindowCollapsed();
      const bool is_current_note_document = (!state_file_path_.empty() && n.path == state_file_path_);
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
        if(ImGui::MenuItem(Lang::t("Rename")))
        {
          rename_win_folder_idx = active_folder_idx_;
          rename_win_note_idx = ni;
          std::snprintf(rename_win_buf, sizeof(rename_win_buf), "%s", n.title.c_str());
          focus_rename_win_input = true;
          open_rename_win_popup = true;
        }
        if(ImGui::MenuItem(Lang::t("Edit")))
        {
          if(editing_mode_ && !is_editing_this)
          {
            normalize_input_text_buffer(markdown_text_);
            state_dirty_ = true;
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
        if(ImGui::MenuItem(Lang::t("Set note color...")))
        {
          color_note_folder_idx = active_folder_idx_;
          color_note_idx = ni;
          note_color_use_default = !n.use_custom_color;
          note_color_buf[0] = n.color_r;
          note_color_buf[1] = n.color_g;
          note_color_buf[2] = n.color_b;
          open_note_color_popup = true;
        }
        if(ImGui::MenuItem(Lang::t("Reset note color"), nullptr, false, n.use_custom_color))
        {
          push_sidebar_snapshot();
          n.use_custom_color = false;
          flash_mark_note(n.path, ImVec4(0.86f, 0.25f, 0.25f, 1.0f));
          save_index();
        }
        if(ImGui::MenuItem(note_is_collapsed ? Lang::t("Expand") : Lang::t("Compact")))
        {
          ImGui::SetWindowCollapsed(window_id.c_str(), !note_is_collapsed, ImGuiCond_Always);
        }
        if(!is_editing_this)
        {
          const std::string preview_text_for_headers = is_current_note_document ? markdown_text_ : read_file_text(n.path);
          const MarkdownSupport::PreviewHeaderStateSummary header_summary =
              summarize_preview_header_states(n.path, preview_text_for_headers);
          auto apply_header_toggle = [&](bool target_open) {
            const std::string preview_state_before = capture_preview_state_snapshot();
            if(set_all_preview_headers_open(n.path, preview_text_for_headers, target_open))
            {
              const std::string preview_state_after = capture_preview_state_snapshot();
              record_preview_history_action(
                  target_open ? "Expand all headers" : "Collapse all headers",
                  n.path,
                  preview_text_for_headers,
                  preview_text_for_headers,
                  preview_state_before,
                  preview_state_after);
            }
          };

          if(header_summary.has_headers && !header_summary.any_expanded)
          {
            if(ImGui::MenuItem(Lang::t("Expand all"))) apply_header_toggle(true);
          }
          else if(header_summary.has_headers && !header_summary.any_collapsed)
          {
            if(ImGui::MenuItem(Lang::t("Collapse all"))) apply_header_toggle(false);
          }
          else if(header_summary.has_headers)
          {
            if(ImGui::MenuItem(Lang::t("Expand all"))) apply_header_toggle(true);
            if(ImGui::MenuItem(Lang::t("Collapse all"))) apply_header_toggle(false);
          }
        }
        if(!n.font_path.empty() && ImGui::MenuItem(Lang::t("Remove custom font")))
        {
          n.font_path.clear();
          save_index();
        }
        {
          constexpr float kDefaultSize = 14.0f;
          constexpr float kMinSize = 8.0f;
          constexpr float kMaxSize = 48.0f;
          const float cur = n.font_size > 0.0f ? n.font_size : kDefaultSize;
          if(ImGui::MenuItem(Lang::t("Increase font size"), nullptr, false, cur < kMaxSize))
          {
            const float next = std::min(kMaxSize, cur + 1.0f);
            n.font_size = (std::fabs(next - kDefaultSize) < 0.5f) ? 0.0f : next;
            save_index();
          }
          if(ImGui::MenuItem(Lang::t("Decrease font size"), nullptr, false, cur > kMinSize))
          {
            const float next = std::max(kMinSize, cur - 1.0f);
            n.font_size = (std::fabs(next - kDefaultSize) < 0.5f) ? 0.0f : next;
            save_index();
          }
          if(n.font_size > 0.0f && ImGui::MenuItem(Lang::t("Reset font size")))
          {
            n.font_size = 0.0f;
            save_index();
          }
        }
        if(note_is_detached && ImGui::MenuItem(Lang::t("Pin above OS windows"), nullptr, n.always_on_top))
        {
          push_sidebar_snapshot();
          n.always_on_top = !n.always_on_top;
          save_index();
        }
        if(ImGui::MenuItem(Lang::t("Hide")))
        {
          push_sidebar_snapshot();
          n.hidden = true;
          if(is_editing_this)
          {
            normalize_input_text_buffer(markdown_text_);
            state_dirty_ = true;
            editing_mode_ = false;
          }
          save_index();
        }
        if(ImGui::MenuItem(multi_selected_here ? Lang::t("Remove selected notes") : Lang::t("Remove note")))
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
      bool table_ctx_right_click_consumed = false;
      bool table_ctx_double_click_consumed = false;

      if(note_window_visible && is_editing_this)
      {
        if(request_undo_edit_ && history_.can_undo())
        {
          apply_undo_snapshot();
          request_undo_edit_ = false;
          request_redo_edit_ = false;
          ImGui::ClearActiveID();
          fmt_folder.typing_word_group = false;
          fmt_folder.deleting_word_group = false;
          fmt_folder.last_edit_cursor = -1;
          refocus_folder_editor = true;
        }
        else if(request_redo_edit_ && history_.can_redo())
        {
          apply_redo_snapshot();
          request_redo_edit_ = false;
          ImGui::ClearActiveID();
          fmt_folder.typing_word_group = false;
          fmt_folder.deleting_word_group = false;
          fmt_folder.last_edit_cursor = -1;
          refocus_folder_editor = true;
        }

        if(request_select_line_)
        {
          const int text_len = static_cast<int>(markdown_text_.size());
          const int pos = std::max(0, std::min(fmt_folder.cursor_pos, text_len));
          int line_start = pos;
          while(line_start > 0 && markdown_text_[static_cast<size_t>(line_start) - 1] != '\n')
            --line_start;
          int line_end = pos;
          while(line_end < text_len && markdown_text_[static_cast<size_t>(line_end)] != '\n')
            ++line_end;
          if(line_end < text_len) ++line_end;
          fmt_folder.cursor_pos = line_end;
          fmt_folder.sel_start = line_start;
          fmt_folder.sel_end = line_end;
          fmt_folder.selection_anchor = line_start;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = line_start;
          fmt_folder.pending_sel_end = line_end;
          refocus_folder_editor = true;
          request_select_line_ = false;
        }

        ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_AllowTabInput |
            ImGuiInputTextFlags_CallbackResize |
            ImGuiInputTextFlags_CallbackEdit |
            ImGuiInputTextFlags_CallbackAlways;
        static MdEditorUserData ud_folder{&markdown_text_, &fmt_folder};
        ud_folder.text = &markdown_text_;

        const bool apply_search_selection =
            search_editor_selection_start_ >= 0 &&
            search_editor_selection_note_path_ == state_file_path_;
        if(apply_search_selection)
        {
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = search_editor_selection_start_;
          fmt_folder.pending_sel_end = search_editor_selection_end_;
          fmt_folder.selection_anchor = search_editor_selection_start_;
          if(!search_window_visible_ && search_editor_selection_focus_pending_)
          {
            refocus_folder_editor = true;
            search_editor_selection_focus_pending_ = false;
          }
        }

        if(search_jump_force_edit_ &&
           !search_jump_note_path_.empty() &&
           search_jump_note_path_ == state_file_path_ &&
           search_jump_pos_ >= 0)
        {
          const int start = std::max(0, std::min(search_jump_pos_, static_cast<int>(markdown_text_.size())));
          const int end = std::max(start, std::min(search_jump_pos_ + std::max(1, search_jump_len_), static_cast<int>(markdown_text_.size())));
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = start;
          fmt_folder.pending_sel_end = end;
          fmt_folder.selection_anchor = start;
          refocus_folder_editor = true;
          search_jump_note_path_.clear();
          search_jump_pos_ = -1;
          search_jump_len_ = 0;
          search_jump_force_edit_ = false;
        }
        if(preview_edit_cursor_pos_ >= 0 &&
           preview_edit_cursor_note_path_ == state_file_path_)
        {
          const int cursor = std::clamp(
              preview_edit_cursor_pos_, 0, static_cast<int>(markdown_text_.size()));
          fmt_folder.cursor_pos = cursor;
          fmt_folder.sel_start = cursor;
          fmt_folder.sel_end = cursor;
          fmt_folder.selection_anchor = cursor;
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = cursor;
          fmt_folder.pending_sel_end = cursor;
          refocus_folder_editor = true;
          preview_edit_cursor_note_path_.clear();
          preview_edit_cursor_pos_ = -1;
        }

        if(refocus_folder_editor)
        {
          fmt_folder.typing_word_group = false;
          fmt_folder.deleting_word_group = false;
          fmt_folder.last_edit_cursor = -1;
          // SetKeyboardFocusHere() sets FromTabbing, which InputTextMultiline blocks
          // when AllowTabInput is set. Activate directly with PreferInput instead.
          GImGui->NavNextActivateId = ImGui::GetID("##md_folder");
          GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput | ImGuiActivateFlags_TryToPreserveState;
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
        if(search_editor_scroll_pos_ >= 0 && search_editor_scroll_note_path_ == state_file_path_ &&
           scroll_input_text_multiline_to_offset(
               ImGui::GetID("##md_folder"), markdown_text_, search_editor_scroll_pos_))
        {
          search_editor_scroll_note_path_.clear();
          search_editor_scroll_pos_ = -1;
        }
        if(search_editor_match_offset_ >= 0 &&
           search_editor_match_note_path_ == state_file_path_)
        {
          draw_input_text_multiline_match_indicator(
              ImGui::GetID("##md_folder"),
              markdown_text_,
              search_editor_match_offset_,
              search_editor_match_length_);
        }
        if(apply_search_selection && ImGui::IsItemActive() && !fmt_folder.pending_select_range)
        {
          search_editor_selection_note_path_.clear();
          search_editor_selection_start_ = -1;
          search_editor_selection_end_ = -1;
          search_editor_selection_focus_pending_ = false;
        }
        normalize_input_text_buffer(markdown_text_);
        if(changed)
        {
          update_pending_text_history(
              "Edit text",
              before_edit,
              markdown_text_,
              should_push_word_granular_undo(before_edit, markdown_text_, fmt_folder));
          state_dirty_ = true;
        }
        const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const ImGuiIO &io = ImGui::GetIO();
        if(editor_hovered && io.MouseClickedCount[ImGuiMouseButton_Left] == 2)
        {
          const auto [ws, we] = word_bounds_from_double_click(markdown_text_, fmt_folder.cursor_pos, fmt_folder.sel_start, fmt_folder.sel_end);
          fmt_folder.pending_select_range = true;
          fmt_folder.pending_sel_start = ws;
          fmt_folder.pending_sel_end = we;
          fmt_folder.selection_anchor = ws;
        }
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
        editor_action_selection_available_ = has_selection;
        const int sel_min = (a < b) ? a : b;
        const int sel_max = (a < b) ? b : a;
        if(has_selection)
        {
          anchor_sel_start = sel_min;
          anchor_sel_end = sel_max;
        }
        (void)editor_hovered;
      }
      else if(note_window_visible)
      {
        preview_text = is_current_note_document ? markdown_text_ : read_file_text(n.path);
        const std::string preview_before = preview_text;
        const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize);
        MarkdownView::set_render_width(preview_w);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + preview_w);
        set_preview_document_path(n.path);
        const MarkdownSupport::PreviewRenderResult preview_result = render_preview_with_task_checkboxes_ex(preview_text);
        const bool preview_changed = preview_result.markdown_changed || preview_result.preview_state_changed;
        changed = preview_result.markdown_changed;
        table_ctx_right_click_consumed = preview_result.consumed_right_click;
        table_ctx_double_click_consumed = preview_result.consumed_double_click;
        ImGui::PopTextWrapPos();
        if(preview_changed)
        {
          const std::string preview_state_after = capture_preview_state_snapshot();
          if(is_current_note_document)
          {
            markdown_text_ = preview_text;
            normalize_input_text_buffer(markdown_text_);
            state_dirty_ = true;
          }
          else if(preview_text != preview_before)
          {
            if(write_text_file(n.path, preview_text))
              update_note_cache(n.path, preview_text);
          }

          record_preview_history_action("Edit preview widget", n.path, preview_before, preview_text, preview_state_before_frame, preview_state_after);
        }
        if(is_current_note_document && request_undo_edit_ && history_.can_undo())
        {
          apply_undo_snapshot();
          request_undo_edit_ = false;
          request_redo_edit_ = false;
          request_undo_draw_ = false;
          request_redo_draw_ = false;
        }
        if(is_current_note_document && request_redo_edit_ && history_.can_redo())
        {
          apply_redo_snapshot();
          request_redo_edit_ = false;
          request_undo_draw_ = false;
          request_redo_draw_ = false;
        }
      }

      const std::string body_popup_id = "Note Body Actions##" + n.id;
      const bool w_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      if(!is_editing_this &&
         w_hovered &&
         !mouse_on_title &&
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
         !table_ctx_right_click_consumed)
      {
        ImGui::OpenPopup(body_popup_id.c_str());
      }
      if(!is_editing_this && ImGui::BeginPopup(body_popup_id.c_str()))
      {
        if(ImGui::MenuItem(Lang::t("Copy all")))
        {
          ImGui::SetClipboardText(preview_text.c_str());
        }
        ImGui::EndPopup();
      }

      if(w_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        if(!(editing_mode_ && !is_editing_this))
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
      }
      if(w_hovered &&
         !mouse_on_title &&
         ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
         !table_ctx_double_click_consumed)
      {
        if(editing_mode_ && !is_editing_this)
        {
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
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

      const float max_preview_h = note_viewport ? note_viewport->Size.y * 0.95f : FLT_MAX;
      const float auto_h = std::max(140.0f, std::min(max_preview_h, ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y));
      const ImVec2 pos = ImGui::GetWindowPos();
      const ImVec2 size = ImGui::GetWindowSize();
      note_rects.push_back(NoteRectInfo{ni, pos, ImVec2(pos.x + size.x, pos.y + size.y)});
      if(selected_note_indices.count(ni) != 0)
      {
        ImGui::GetWindowDrawList()->AddRect(
            pos,
            ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::GetColorU32(with_alpha(neutral_sel, 0.95f)),
            0.0f,
            0,
            2.0f);
      }

      if(!is_editing_this && std::fabs(auto_h - size.y) > 1.5f)
      {
        ImGui::SetWindowSize(ImVec2(size.x, auto_h));
      }

      apply_viewport_always_on_top(note_viewport, window_, n.always_on_top);
      if(note_is_detached && n.always_on_top && note_viewport != nullptr) pinned_topmost_viewports_.insert(note_viewport->ID);

      ImVec2 effective_pos = pos;
      const bool allow_platform_windows = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
      const bool window_is_minimized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
      const ImVec2 clamped_pos(
          std::max(bg_p0.x, std::min(pos.x, std::max(bg_p0.x, bg_p1.x - size.x))),
          std::max(bg_p0.y, std::min(pos.y, std::max(bg_p0.y, bg_p1.y - size.y))));
      if(!allow_platform_windows && !force_note_layout_restore_ && !window_is_minimized &&
         (std::fabs(clamped_pos.x - pos.x) > 0.01f || std::fabs(clamped_pos.y - pos.y) > 0.01f))
      {
        ImGui::SetWindowPos(window_id.c_str(), clamped_pos, ImGuiCond_Always);
        effective_pos = clamped_pos;
      }

      const ImVec2 note_delta(effective_pos.x - old_pos_x, effective_pos.y - old_pos_y);
      if((std::fabs(note_delta.x) > 0.01f || std::fabs(note_delta.y) > 0.01f) &&
         ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) &&
         !note_drag_snapshot_taken)
      {
        push_selection_snapshot();
        note_drag_snapshot_taken = true;
      }
      if(selected_note_indices.count(ni) != 0 &&
         (std::fabs(note_delta.x) > 0.01f || std::fabs(note_delta.y) > 0.01f) &&
         ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
      {
        pending_group_delta = note_delta;
        pending_group_mover = ni;
      }

      auto changed_f = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
      if(!window_is_minimized && (changed_f(n.pos_x, effective_pos.x) || changed_f(n.pos_y, effective_pos.y) || changed_f(n.width, size.x) || changed_f(n.height, auto_h) || !n.has_layout))
      {
        n.pos_x = effective_pos.x;
        n.pos_y = effective_pos.y;
        n.width = size.x;
        n.height = auto_h;
        n.has_layout = true;
        layout_dirty_ = true;
      }

      // Image drop target: drop from Explorer panel onto this note window
      if(ImGui::BeginDragDropTarget())
      {
        if(const ImGuiPayload *payload =
               ImGui::AcceptDragDropPayload("NOTEPP_IMAGE_INSERT"))
        {
          const char *img_path_raw = static_cast<const char *>(payload->Data);
          const std::filesystem::path img_path_fs(img_path_raw);
          const std::string img_alt = img_path_fs.stem().string();
          std::string img_rel;
          try
          {
            img_rel = std::filesystem::relative(img_path_fs, std::filesystem::path(n.path).parent_path()).string();
          }
          catch(...)
          {
            img_rel = img_path_fs.filename().string();
          }
          const std::string img_insert = "\n![" + img_alt + "](" + img_rel + ")\n";

          if(is_editing_this)
          {
            const int insert_pos =
                (fmt_folder.cursor_pos >= 0 &&
                 fmt_folder.cursor_pos <= (int)markdown_text_.size())
                    ? fmt_folder.cursor_pos
                    : (int)markdown_text_.size();
            markdown_text_.insert((size_t)insert_pos, img_insert);
            const int new_cursor = insert_pos + (int)img_insert.size();
            fmt_folder.pending_select_range = true;
            fmt_folder.pending_sel_start = new_cursor;
            fmt_folder.pending_sel_end = new_cursor;
            refocus_folder_editor = true;
            state_dirty_ = true;
          }
          else
          {
            std::string note_content = read_file_text(n.path);
            note_content += img_insert;
            write_text_file(n.path, note_content);
            if(is_current_note_document)
              markdown_text_ = note_content;
          }
          flash_mark_note(n.path, ImVec4(0.25f, 0.70f, 0.96f, 1.0f));
        }
        if(const ImGuiPayload *payload =
               ImGui::AcceptDragDropPayload("NOTEPP_FONT_SET"))
        {
          const char *font_abs = static_cast<const char *>(payload->Data);
          if(font_abs)
          {
            n.font_path = std::filesystem::path(font_abs).lexically_normal().string();
            save_index();
            flash_mark_note(n.path, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
          }
        }
        ImGui::EndDragDropTarget();
      }

      if(note_font)
      {
        ImGui::PopFont();
        MarkdownView::set_fonts(font_regular_, font_italic_, font_bold_);
      }

      ImGui::End();
      ImGui::PopStyleColor(folder_theme_count);

      if(!note_window_open)
      {
        push_sidebar_snapshot();
        n.hidden = true;
        if(is_editing_this)
        {
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
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
            const std::string del_path = f.notes[(size_t)idx].path;
            if(!del_path.empty() && !soft_delete_project_file(del_path)) continue;
            queue_pending_delete_path(del_path);
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
      if(!layout_locked_)
      {
        for(int idx : selected_note_indices)
        {
          if(idx == pending_group_mover) continue;
          if(idx < 0 || idx >= (int)f.notes.size()) continue;
          NoteMeta &sn = f.notes[(size_t)idx];
          if(sn.hidden) continue;
          sn.pos_x += pending_group_delta.x;
          sn.pos_y += pending_group_delta.y;
          const std::string sid = sn.title + "###FolderNote_" + sn.id;
          ImGui::SetWindowPos(sid.c_str(), ImVec2(sn.pos_x, sn.pos_y), ImGuiCond_Always);
          layout_dirty_ = true;
        }

        if(!selected_stroke_indices.empty())
        {
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
    }
    force_note_layout_restore_ = false;
    request_reset_layout = false;

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
      state_dirty_ = true;
      editing_mode_ = false;
      request_exit_edit_mode_ = false;
      editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
      request_close_editor_action_ui_widget_chooser_ = false;
    }

    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !deferred_selection_snapshot_before.empty())
    {
      record_workspace_after("Move selection", std::move(deferred_selection_snapshot_before));
      deferred_selection_snapshot_before.clear();
    }
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !deferred_draw_snapshot_before.empty())
    {
      record_workspace_after("Edit drawings", std::move(deferred_draw_snapshot_before));
      deferred_draw_snapshot_before.clear();
    }
    if(!deferred_sidebar_snapshot_before.empty())
    {
      record_workspace_after("Edit workspace", std::move(deferred_sidebar_snapshot_before));
      deferred_sidebar_snapshot_before.clear();
    }

    if(notepp::sync_coordinator::project_writes_allowed(git_sync_in_progress_))
    {
      if(layout_dirty_ && !ImGui::IsAnyMouseDown())
      {
        const bool index_saved = pending_command_mutations_.empty() ? save_index() : true;
        capture_to_active_profile();
        const bool profiles_saved = save_profiles();
        if(index_saved && profiles_saved) layout_dirty_ = false;
      }
      if(state_dirty_ && save_state()) state_dirty_ = false;
      if(g_drawings_dirty && !ImGui::IsAnyMouseDown()) save_drawings_state();
      if(g_clipboard_dirty && !ImGui::IsAnyMouseDown()) save_note_clipboard();
    }
    render_search_dialog();
    render_note_switcher();
    render_debug_history_window();
    render_terminal();
    render_command_finder();
    render_editor_actions();
    if(git_disabled_for_frame) ImGui::EndDisabled();
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

    ImGui::SetNextWindowDockID(ImGui::GetID("MyDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 180.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "Note###NoteWindowEmpty",
        nullptr,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(Lang::t("This folder has no notes."));
    ImGui::TextUnformatted(Lang::t("Right click to create a note."));
    ImGui::End();

    if(!deferred_sidebar_snapshot_before.empty())
    {
      record_workspace_after("Edit workspace", std::move(deferred_sidebar_snapshot_before));
      deferred_sidebar_snapshot_before.clear();
    }
    render_search_dialog();
    render_note_switcher();
    render_debug_history_window();
    render_terminal();
    render_command_finder();
    render_editor_actions();
    if(notepp::sync_coordinator::project_writes_allowed(git_sync_in_progress_))
    {
      if(g_drawings_dirty && !ImGui::IsAnyMouseDown()) save_drawings_state();
      if(g_clipboard_dirty && !ImGui::IsAnyMouseDown()) save_note_clipboard();
    }
    if(git_disabled_for_frame) ImGui::EndDisabled();
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

  std::string note_window_label = note_title_ + "###NoteWindow";
  const ImGuiID active_ini_dock_id = saved_window_dock_id(note_window_label.c_str());
  const bool can_restore_active_dock = dockers_enabled_ && imgui_docking_enabled();
  if(can_restore_active_dock && force_note_layout_restore_ && active_note.dock_id == 0 &&
     active_ini_dock_id != 0)
  {
    active_note.dock_id = active_ini_dock_id;
    layout_dirty_ = true;
  }

  const bool restore_active_dock =
      can_restore_active_dock && force_note_layout_restore_ && active_note.dock_id != 0;
  if(restore_active_dock)
    ImGui::SetNextWindowDockID(active_note.dock_id, ImGuiCond_Always);

  if(active_note.has_layout)
  {
    const ImGuiCond cond = force_note_layout_restore_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    if(!restore_active_dock)
    {
      ImGui::SetNextWindowPos(ImVec2(active_note.pos_x, active_note.pos_y), cond);
      ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, active_note.width), std::max(140.0f, active_note.height)), cond);
    }
  }
  else
  {
    if(can_restore_active_dock)
      ImGui::SetNextWindowDockID(ImGui::GetID("MyDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, note_window_height), ImGuiCond_FirstUseEver);
  }

  const float pre_viewport_h = ImGui::GetMainViewport()->Size.y;

  ImGui::SetNextWindowSizeConstraints(
      ImVec2(320.0f, 140.0f),
      ImVec2(FLT_MAX, pre_viewport_h));
  if(search_request_window_focus_) ImGui::SetNextWindowFocus();

  const int active_folder_theme_count = push_folder_imgui_theme(
      make_note_theme(
          active_note.use_custom_color,
          active_note.color_r,
          active_note.color_g,
          active_note.color_b,
          ImGui::GetStyle()),
      ImGui::GetStyle());
  ImGui::Begin(
      note_window_label.c_str(),
      nullptr,
      (layout_locked_ ? ImGuiWindowFlags_NoMove : 0) |
          (!dockers_enabled_ ? ImGuiWindowFlags_NoDocking : 0));
  if(search_request_window_focus_) search_request_window_focus_ = false;
  const ImGuiID active_current_dock_id = ImGui::GetWindowDockID();
  if(can_restore_active_dock && active_note.dock_id != active_current_dock_id)
  {
    active_note.dock_id = active_current_dock_id;
    layout_dirty_ = true;
  }

  // Right click on title bar for note window actions.
  static bool open_rename_popup = false;
  static char rename_buf[256] = {};
  const ImVec2 win_pos = ImGui::GetWindowPos();
  const float title_bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
  const ImVec2 mouse_pos = ImGui::GetMousePos();
  const std::string active_note_actions_popup_id = "Note Window Actions###ActiveNoteWindow";
  ImGuiViewport *active_note_viewport = ImGui::GetWindowViewport();
  const bool active_note_detached = viewport_is_detached_from_main(active_note_viewport, window_);
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
    ImGui::OpenPopup(active_note_actions_popup_id.c_str());
  }
  if(ImGui::BeginPopup(active_note_actions_popup_id.c_str()))
  {
    if(ImGui::MenuItem(Lang::t("Rename")))
    {
      std::snprintf(rename_buf, sizeof(rename_buf), "%s", note_title_.c_str());
      open_rename_popup = true;
    }
    if(active_note_detached && ImGui::MenuItem("Pin above OS windows", nullptr, active_note.always_on_top))
    {
      push_sidebar_snapshot();
      active_note.always_on_top = !active_note.always_on_top;
      save_index();
    }
    ImGui::EndPopup();
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
    ImGui::TextUnformatted(Lang::t("Name"));
    ImGui::SetNextItemWidth(260.0f);
    if(ImGui::InputText("##rename_note_title", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      perform_workspace_change("Rename note", [&]() {
        normalize_input_text_buffer(markdown_text_);
        rename_note_storage_for_title(rename_buf);
      });
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  apply_viewport_always_on_top(active_note_viewport, window_, active_note.always_on_top);
  if(active_note_detached && active_note.always_on_top && active_note_viewport != nullptr) pinned_topmost_viewports_.insert(active_note_viewport->ID);

  static bool show_palette = false;
  static bool refocus_editor = false;
  if(request_focus_editor_)
  {
    refocus_editor = true;
    request_focus_editor_ = false;
  }
  static MdFormatState fmt;
  static MdEditorUserData ud{&markdown_text_, &fmt};
  ud.text = &markdown_text_;

  const bool apply_search_selection =
      search_editor_selection_start_ >= 0 &&
      search_editor_selection_note_path_ == state_file_path_;
  if(apply_search_selection)
  {
    fmt.pending_select_range = true;
    fmt.pending_sel_start = search_editor_selection_start_;
    fmt.pending_sel_end = search_editor_selection_end_;
    fmt.selection_anchor = search_editor_selection_start_;
    if(!search_window_visible_ && search_editor_selection_focus_pending_)
    {
      refocus_editor = true;
      search_editor_selection_focus_pending_ = false;
    }
  }

  if(search_jump_force_edit_ &&
     !search_jump_note_path_.empty() &&
     search_jump_note_path_ == state_file_path_ &&
     search_jump_pos_ >= 0)
  {
    editing_mode_ = true;
    const int start = std::max(0, std::min(search_jump_pos_, static_cast<int>(markdown_text_.size())));
    const int end = std::max(start, std::min(search_jump_pos_ + std::max(1, search_jump_len_), static_cast<int>(markdown_text_.size())));
    fmt.pending_select_range = true;
    fmt.pending_sel_start = start;
    fmt.pending_sel_end = end;
    fmt.selection_anchor = start;
    refocus_editor = true;
    search_jump_note_path_.clear();
    search_jump_pos_ = -1;
    search_jump_len_ = 0;
    search_jump_force_edit_ = false;
  }
  if(editing_mode_ && preview_edit_cursor_pos_ >= 0 &&
     preview_edit_cursor_note_path_ == state_file_path_)
  {
    const int cursor = std::clamp(
        preview_edit_cursor_pos_, 0, static_cast<int>(markdown_text_.size()));
    fmt.cursor_pos = cursor;
    fmt.sel_start = cursor;
    fmt.sel_end = cursor;
    fmt.selection_anchor = cursor;
    fmt.pending_select_range = true;
    fmt.pending_sel_start = cursor;
    fmt.pending_sel_end = cursor;
    refocus_editor = true;
    preview_edit_cursor_note_path_.clear();
    preview_edit_cursor_pos_ = -1;
  }

  if(!editing_mode_)
  {
    // Preview mode (interactive)
    const std::string preview_before = markdown_text_;
    const std::string preview_state_before = capture_preview_state_snapshot();
    const float preview_w = std::max(8.0f, ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize);
    MarkdownView::set_render_width(preview_w);

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + preview_w);
    set_preview_document_path(state_file_path_);
    const MarkdownSupport::PreviewRenderResult preview_result = render_preview_with_task_checkboxes_ex(markdown_text_);
    const bool preview_changed = preview_result.markdown_changed || preview_result.preview_state_changed;
    const bool table_double_click_consumed = preview_result.consumed_double_click;
    ImGui::PopTextWrapPos();

    if(preview_changed)
    {
      const std::string preview_state_after = capture_preview_state_snapshot();
      state_dirty_ = true;
      record_preview_history_action("Edit preview widget", state_file_path_, preview_before, markdown_text_, preview_state_before, preview_state_after);
    }
    if(request_undo_edit_ && history_.can_undo())
    {
      apply_undo_snapshot();
      request_undo_edit_ = false;
      request_redo_edit_ = false;
      request_undo_draw_ = false;
      request_redo_draw_ = false;
    }
    if(request_redo_edit_ && history_.can_redo())
    {
      apply_redo_snapshot();
      request_redo_edit_ = false;
      request_undo_draw_ = false;
      request_redo_draw_ = false;
    }

    // Enter edit mode only on double click (single click does nothing)
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
       ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
       !table_double_click_consumed)
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
    if(request_undo_edit_ && history_.can_undo())
    {
      apply_undo_snapshot();
      request_undo_edit_ = false;
      request_redo_edit_ = false;
      request_undo_draw_ = false;
      request_redo_draw_ = false;
      ImGui::ClearActiveID();
      fmt.typing_word_group = false;
      fmt.deleting_word_group = false;
      fmt.last_edit_cursor = -1;
      refocus_editor = true;
    }
    else if(request_redo_edit_ && history_.can_redo())
    {
      apply_redo_snapshot();
      request_redo_edit_ = false;
      request_undo_draw_ = false;
      request_redo_draw_ = false;
      ImGui::ClearActiveID();
      fmt.typing_word_group = false;
      fmt.deleting_word_group = false;
      fmt.last_edit_cursor = -1;
      refocus_editor = true;
    }

    if(request_select_line_)
    {
      const int text_len = static_cast<int>(markdown_text_.size());
      const int pos = std::max(0, std::min(fmt.cursor_pos, text_len));
      int line_start = pos;
      while(line_start > 0 && markdown_text_[static_cast<size_t>(line_start) - 1] != '\n')
        --line_start;
      int line_end = pos;
      while(line_end < text_len && markdown_text_[static_cast<size_t>(line_end)] != '\n')
        ++line_end;
      if(line_end < text_len) ++line_end;
      fmt.cursor_pos = line_end;
      fmt.sel_start = line_start;
      fmt.sel_end = line_end;
      fmt.selection_anchor = line_start;
      fmt.pending_select_range = true;
      fmt.pending_sel_start = line_start;
      fmt.pending_sel_end = line_end;
      refocus_editor = true;
      request_select_line_ = false;
    }

    ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_AllowTabInput |
        ImGuiInputTextFlags_CallbackResize |
        ImGuiInputTextFlags_CallbackEdit |
        ImGuiInputTextFlags_CallbackAlways;
    if(refocus_editor)
    {
      fmt.typing_word_group = false;
      fmt.deleting_word_group = false;
      fmt.last_edit_cursor = -1;
      GImGui->NavNextActivateId = ImGui::GetID("##md");
      GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput | ImGuiActivateFlags_TryToPreserveState;
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
    if(search_editor_scroll_pos_ >= 0 && search_editor_scroll_note_path_ == state_file_path_ &&
       scroll_input_text_multiline_to_offset(
           ImGui::GetID("##md"), markdown_text_, search_editor_scroll_pos_))
    {
      search_editor_scroll_note_path_.clear();
      search_editor_scroll_pos_ = -1;
    }
    if(search_editor_match_offset_ >= 0 &&
       search_editor_match_note_path_ == state_file_path_)
    {
      draw_input_text_multiline_match_indicator(
          ImGui::GetID("##md"),
          markdown_text_,
          search_editor_match_offset_,
          search_editor_match_length_);
    }
    if(apply_search_selection && ImGui::IsItemActive() && !fmt.pending_select_range)
    {
      search_editor_selection_note_path_.clear();
      search_editor_selection_start_ = -1;
      search_editor_selection_end_ = -1;
      search_editor_selection_focus_pending_ = false;
    }
    normalize_input_text_buffer(markdown_text_);
    if(text_changed)
    {
      update_pending_text_history(
          "Edit text",
          before_edit,
          markdown_text_,
          should_push_word_granular_undo(before_edit, markdown_text_, fmt));
      state_dirty_ = true;
    }
    // Image drop target for the single-note editor
    if(ImGui::BeginDragDropTarget())
    {
      if(const ImGuiPayload *payload =
             ImGui::AcceptDragDropPayload("NOTEPP_IMAGE_INSERT"))
      {
        const char *img_path_raw = static_cast<const char *>(payload->Data);
        const std::filesystem::path img_path_fs(img_path_raw);
        const std::string img_alt = img_path_fs.stem().string();
        std::string img_rel;
        try
        {
          img_rel = std::filesystem::relative(img_path_fs, std::filesystem::path(state_file_path_).parent_path()).string();
        }
        catch(...)
        {
          img_rel = img_path_fs.filename().string();
        }
        const std::string img_insert = "\n![" + img_alt + "](" + img_rel + ")\n";
        const int insert_pos =
            (fmt.cursor_pos >= 0 && fmt.cursor_pos <= (int)markdown_text_.size())
                ? fmt.cursor_pos
                : (int)markdown_text_.size();
        const std::string before_drop = markdown_text_;
        markdown_text_.insert((size_t)insert_pos, img_insert);
        const int new_cursor = insert_pos + (int)img_insert.size();
        fmt.pending_select_range = true;
        fmt.pending_sel_start = new_cursor;
        fmt.pending_sel_end = new_cursor;
        refocus_editor = true;
        update_pending_text_history("Insert image", before_drop, markdown_text_, true);
        state_dirty_ = true;
      }
      ImGui::EndDragDropTarget();
    }
    // After the widget: show popup if selection is non-empty and editor is focused/active
    const bool editor_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImGuiIO &io = ImGui::GetIO();
    if(editor_hovered && io.MouseClickedCount[ImGuiMouseButton_Left] == 2)
    {
      const auto [ws, we] = word_bounds_from_double_click(markdown_text_, fmt.cursor_pos, fmt.sel_start, fmt.sel_end);
      fmt.pending_select_range = true;
      fmt.pending_sel_start = ws;
      fmt.pending_sel_end = we;
      fmt.selection_anchor = ws;
    }
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
    editor_action_selection_available_ = has_selection;
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
    auto insert_note_snippet = [&](std::string snippet, bool wrap_in_ui_fences) {
      static constexpr const char *kCursorMarker = "__CURSOR__";
      int cursor_offset = static_cast<int>(snippet.size());
      const std::size_t marker_pos = snippet.find(kCursorMarker);
      if(marker_pos != std::string::npos)
      {
        cursor_offset = static_cast<int>(marker_pos);
        snippet.erase(marker_pos, std::strlen(kCursorMarker));
      }
      const int cursor_pos = std::clamp(fmt.cursor_pos, 0, static_cast<int>(markdown_text_.size()));
      if(wrap_in_ui_fences && !cursor_is_inside_ui_block(markdown_text_, cursor_pos))
      {
        std::string wrapped = "```ui\n";
        cursor_offset += static_cast<int>(wrapped.size());
        wrapped += snippet;
        if(!snippet.empty() && snippet.back() != '\n') wrapped += '\n';
        wrapped += "```\n";
        snippet = std::move(wrapped);
      }
      if(cursor_pos > 0 && !snippet.empty() &&
         markdown_text_[static_cast<std::size_t>(cursor_pos) - 1] != '\n' &&
         snippet.front() != '\n')
      {
        snippet.insert(snippet.begin(), '\n');
        ++cursor_offset;
      }
      push_undo_snapshot();
      markdown_text_.insert(static_cast<std::size_t>(cursor_pos), snippet);
      const int new_cursor = cursor_pos + std::max(0, cursor_offset);
      fmt.cursor_pos = new_cursor;
      fmt.sel_start = new_cursor;
      fmt.sel_end = new_cursor;
      fmt.selection_anchor = new_cursor;
      fmt.pending_select_range = true;
      fmt.pending_sel_start = new_cursor;
      fmt.pending_sel_end = new_cursor;
      normalize_input_text_buffer(markdown_text_);
      state_dirty_ = true;
      refocus_editor = true;
    };

    if(request_editor_action_ != EditorAction::none)
    {
      const bool selection_action =
          request_editor_action_ == EditorAction::bold ||
          request_editor_action_ == EditorAction::italic ||
          request_editor_action_ == EditorAction::strikethrough ||
          request_editor_action_ == EditorAction::quote ||
          request_editor_action_ == EditorAction::color;
      if(!selection_action || has_anchor_selection)
      {
        switch(request_editor_action_)
        {
        case EditorAction::task_list:
          push_undo_snapshot();
          insert_checklist_item_at_cursor(markdown_text_, fmt);
          fmt.sel_start = fmt.cursor_pos;
          fmt.sel_end = fmt.cursor_pos;
          fmt.selection_anchor = fmt.cursor_pos;
          fmt.pending_select_range = true;
          fmt.pending_sel_start = fmt.cursor_pos;
          fmt.pending_sel_end = fmt.cursor_pos;
          break;
        case EditorAction::table:
          push_undo_snapshot();
          insert_markdown_table_at_cursor(markdown_text_, fmt, 2, 2);
          fmt.pending_select_range = true;
          fmt.pending_sel_start = fmt.sel_start;
          fmt.pending_sel_end = fmt.sel_end;
          break;
        case EditorAction::bold:
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
          break;
        case EditorAction::italic:
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
          break;
        case EditorAction::strikethrough:
          push_undo_snapshot();
          apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
          break;
        case EditorAction::quote:
          push_undo_snapshot();
          apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
          break;
        case EditorAction::color:
          push_undo_snapshot();
          apply_color_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, rgba_to_hex(fmt.color));
          break;
        case EditorAction::ui_block:
          editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::open_requested;
          break;
        case EditorAction::none:
          break;
        }
        if(selection_action)
        {
          fmt.sel_start = anchor_sel_start;
          fmt.sel_end = anchor_sel_end;
          fmt.cursor_pos = anchor_sel_end;
          fmt.selection_anchor = anchor_sel_start;
          fmt.pending_select_range = true;
          fmt.pending_sel_start = anchor_sel_start;
          fmt.pending_sel_end = anchor_sel_end;
        }
        if(request_editor_action_ != EditorAction::ui_block)
        {
          normalize_input_text_buffer(markdown_text_);
          state_dirty_ = true;
          refocus_editor = true;
        }
      }
      request_editor_action_ = EditorAction::none;
    }

    if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open_requested)
    {
      ImGui::SetNextWindowFocus();
      ImGui::OpenPopup(kEditorActionsUiWidgetChooserPopup);
    }
    const bool ui_widget_chooser_open =
        editor_action_ui_widget_chooser_state_ != UiWidgetChooserState::closed &&
        render_ui_widget_chooser(
            kEditorActionsUiWidgetChooserPopup,
            true,
            request_close_editor_action_ui_widget_chooser_,
            insert_note_snippet,
            [&]() {
              if(active_folder_idx_ < 0 || active_folder_idx_ >= (int)folders_.size()) return;
              perform_workspace_change("Create .globals.md", [&]() {
                FolderMeta &folder = folders_[(size_t)active_folder_idx_];
                const std::string globals_title = ".globals";
                const std::string globals_path = make_note_path(folder.name, globals_title);
                const auto found = std::find_if(
                    folder.notes.begin(), folder.notes.end(),
                    [&](const NoteMeta &note) { return note.path == globals_path; });
                if(found == folder.notes.end())
                {
                  NoteMeta globals;
                  globals.id = generate_uuid();
                  globals.title = globals_title;
                  globals.path = globals_path;
                  if(!std::filesystem::exists(globals_path))
                    write_text_file(globals_path,
                                    "```ui\ncampaign(\"My Campaign\")\nparty_level(1)\ngold(0)\n```\n");
                  folder.notes.push_back(std::move(globals));
                  active_note_idx_ = static_cast<int>(folder.notes.size()) - 1;
                  load_note_content_for_active();
                  save_index();
                  return;
                }
                active_note_idx_ = static_cast<int>(std::distance(folder.notes.begin(), found));
                load_note_content_for_active();
                show_history_indicator("Opened", ".globals.md", ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
              });
            });
    if(request_close_editor_action_ui_widget_chooser_)
    {
      editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
      request_close_editor_action_ui_widget_chooser_ = false;
    }
    else if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open_requested &&
            ui_widget_chooser_open)
    {
      editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::open;
    }
    else if(editor_action_ui_widget_chooser_state_ == UiWidgetChooserState::open &&
            !ui_widget_chooser_open)
    {
      editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
    }

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

      if(ImGui::Button(Lang::t("Italic")))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "*", "*");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button(Lang::t("Bold")))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "**", "**");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button(Lang::t("Strike")))
      {
        push_undo_snapshot();
        apply_wrap_string(markdown_text_, anchor_sel_start, anchor_sel_end, "~~", "~~");
        applied = true;
      }
      ImGui::SameLine();
      if(ImGui::Button(Lang::t("Note_format")))
      {
        push_undo_snapshot();
        apply_note_quote(markdown_text_, anchor_sel_start, anchor_sel_end);
        applied = true;
      }

      ImGui::Separator();

      ImGui::ColorEdit3(Lang::t("Color"), (float *)&fmt.color, ImGuiColorEditFlags_NoInputs);
      ImGui::SameLine();
      if(ImGui::Button(Lang::t("Apply")))
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
        state_dirty_ = true;
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
    state_dirty_ = true;
    editing_mode_ = false;
    request_exit_edit_mode_ = false;
    editor_action_ui_widget_chooser_state_ = UiWidgetChooserState::closed;
    request_close_editor_action_ui_widget_chooser_ = false;
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
    const bool is_docked = ImGui::IsWindowDocked();
    const float stored_height = is_docked ? size.y : note_window_height;
    auto changed_f = [](float a, float b) { return std::fabs(a - b) > 0.5f; };
    if(changed_f(active_note.pos_x, pos.x) || changed_f(active_note.pos_y, pos.y) ||
       changed_f(active_note.width, size.x) || changed_f(active_note.height, stored_height) ||
       !active_note.has_layout)
    {
      active_note.pos_x = pos.x;
      active_note.pos_y = pos.y;
      active_note.width = size.x;
      active_note.height = stored_height;
      active_note.has_layout = true;
      layout_dirty_ = true;
    }
  }

  ImGui::End();
  ImGui::PopStyleColor(active_folder_theme_count);
  force_note_layout_restore_ = false;

  if(!deferred_sidebar_snapshot_before.empty())
  {
    record_workspace_after("Edit workspace", std::move(deferred_sidebar_snapshot_before));
    deferred_sidebar_snapshot_before.clear();
  }

  if(notepp::sync_coordinator::project_writes_allowed(git_sync_in_progress_))
  {
    if(layout_dirty_ && !ImGui::IsAnyMouseDown())
    {
      const bool index_saved = pending_command_mutations_.empty() ? save_index() : true;
      capture_to_active_profile();
      const bool profiles_saved = save_profiles();
      if(index_saved && profiles_saved) layout_dirty_ = false;
    }
    if(state_dirty_ && save_state()) state_dirty_ = false;
    if(g_drawings_dirty && !ImGui::IsAnyMouseDown()) save_drawings_state();
    if(g_clipboard_dirty && !ImGui::IsAnyMouseDown()) save_note_clipboard();
  }
  render_search_dialog();
  render_note_switcher();
  render_debug_history_window();
  render_terminal();
  render_command_finder();
  render_editor_actions();
  if(git_disabled_for_frame) ImGui::EndDisabled();

}
