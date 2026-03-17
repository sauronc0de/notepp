#include "markdown_code_highlight.hpp"

#include <tree_sitter/api.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "tree_sitter_queries.hpp"

namespace
{
extern "C" const TSLanguage *tree_sitter_bash();
extern "C" const TSLanguage *tree_sitter_c();
extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_python();

using MarkdownCodeHighlight::HighlightKind;
using MarkdownCodeHighlight::HighlightSpan;
using MarkdownCodeHighlight::HighlightedCodeBlock;
using MarkdownCodeHighlight::LanguageDefinition;

struct RenderTheme
{
  ImVec4 background = ImVec4(0.11f, 0.13f, 0.16f, 1.0f);
  ImVec4 border = ImVec4(0.24f, 0.29f, 0.35f, 1.0f);
  ImVec4 gutter = ImVec4(0.47f, 0.54f, 0.63f, 1.0f);
  ImVec4 label = ImVec4(0.71f, 0.79f, 0.92f, 1.0f);
  ImVec4 plain = ImVec4(0.89f, 0.91f, 0.94f, 1.0f);
  ImVec4 comment = ImVec4(0.45f, 0.63f, 0.52f, 1.0f);
  ImVec4 keyword = ImVec4(0.97f, 0.63f, 0.39f, 1.0f);
  ImVec4 string_ = ImVec4(0.85f, 0.79f, 0.51f, 1.0f);
  ImVec4 function = ImVec4(0.49f, 0.76f, 0.95f, 1.0f);
  ImVec4 variable = ImVec4(0.91f, 0.91f, 0.94f, 1.0f);
  ImVec4 type = ImVec4(0.56f, 0.82f, 0.75f, 1.0f);
  ImVec4 number = ImVec4(0.78f, 0.70f, 0.95f, 1.0f);
  ImVec4 op = ImVec4(0.92f, 0.93f, 0.95f, 1.0f);
  ImVec4 property = ImVec4(0.63f, 0.84f, 0.98f, 1.0f);
  ImVec4 constant = ImVec4(0.98f, 0.73f, 0.50f, 1.0f);
  ImVec4 punctuation = ImVec4(0.73f, 0.77f, 0.84f, 1.0f);
};

struct Registry
{
  std::mutex mutex;
  std::vector<LanguageDefinition> languages;
};

Registry &registry()
{
  static Registry instance;
  return instance;
}

std::string lower_trimmed(std::string_view text)
{
  size_t start = 0;
  while(start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;

  size_t end = text.size();
  while(end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;

  std::string out;
  out.reserve(end - start);
  for(size_t i = start; i < end; ++i)
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
  return out;
}

std::string normalize_alias(std::string_view alias)
{
  std::string out;
  const std::string lowered = lower_trimmed(alias);
  out.reserve(lowered.size());
  for(char c : lowered)
  {
    const unsigned char uc = static_cast<unsigned char>(c);
    if(std::isalnum(uc) || c == '_' || c == '+' || c == '-' || c == '#')
      out.push_back(c);
  }
  return out;
}

std::string fence_language_token(std::string_view fence_info)
{
  const std::string lowered = lower_trimmed(fence_info);
  size_t end = lowered.find_first_of(" \t{");
  const std::string_view token = (end == std::string::npos) ? std::string_view(lowered) : std::string_view(lowered).substr(0, end);
  return normalize_alias(token);
}

void register_builtin_languages()
{
  static std::once_flag once;
  std::call_once(once, []() {
    MarkdownCodeHighlight::register_language({
        .name = "c",
        .aliases = {"c", "h"},
        .language_factory = tree_sitter_c,
        .highlight_query = MarkdownCodeHighlight::BuiltinQueries::kTreeSitterCHighlights,
    });
    MarkdownCodeHighlight::register_language({
        .name = "cpp",
        .aliases = {"cpp", "c++", "cc", "cxx", "hpp", "hh", "hxx"},
        .language_factory = tree_sitter_cpp,
        .highlight_query = MarkdownCodeHighlight::BuiltinQueries::kTreeSitterCppHighlights,
    });
    MarkdownCodeHighlight::register_language({
        .name = "python",
        .aliases = {"python", "py"},
        .language_factory = tree_sitter_python,
        .highlight_query = MarkdownCodeHighlight::BuiltinQueries::kTreeSitterPythonHighlights,
    });
    MarkdownCodeHighlight::register_language({
        .name = "bash",
        .aliases = {"bash", "sh", "shell", "zsh"},
        .language_factory = tree_sitter_bash,
        .highlight_query = MarkdownCodeHighlight::BuiltinQueries::kTreeSitterBashHighlights,
    });
  });
}

int kind_priority(HighlightKind kind)
{
  switch(kind)
  {
  case HighlightKind::Comment:
    return 100;
  case HighlightKind::String:
    return 95;
  case HighlightKind::Keyword:
    return 90;
  case HighlightKind::Function:
    return 85;
  case HighlightKind::Type:
    return 80;
  case HighlightKind::Constant:
    return 75;
  case HighlightKind::Number:
    return 70;
  case HighlightKind::Property:
    return 65;
  case HighlightKind::Variable:
    return 60;
  case HighlightKind::Operator:
    return 55;
  case HighlightKind::Punctuation:
    return 50;
  case HighlightKind::Plain:
  default:
    return 0;
  }
}

HighlightKind classify_capture_name(std::string_view capture_name)
{
  if(capture_name.rfind("comment", 0) == 0) return HighlightKind::Comment;
  if(capture_name.rfind("string", 0) == 0) return HighlightKind::String;
  if(capture_name.rfind("keyword", 0) == 0) return HighlightKind::Keyword;
  if(capture_name.rfind("function", 0) == 0 || capture_name.rfind("constructor", 0) == 0) return HighlightKind::Function;
  if(capture_name.rfind("type", 0) == 0) return HighlightKind::Type;
  if(capture_name.rfind("constant", 0) == 0 || capture_name == "boolean") return HighlightKind::Constant;
  if(capture_name.rfind("number", 0) == 0 || capture_name == "float" || capture_name == "integer") return HighlightKind::Number;
  if(capture_name.rfind("property", 0) == 0 || capture_name.rfind("field", 0) == 0) return HighlightKind::Property;
  if(capture_name.rfind("parameter", 0) == 0 || capture_name.rfind("variable", 0) == 0) return HighlightKind::Variable;
  if(capture_name.rfind("operator", 0) == 0) return HighlightKind::Operator;
  if(capture_name.rfind("punctuation", 0) == 0) return HighlightKind::Punctuation;
  return HighlightKind::Plain;
}

ImVec4 color_for_kind(HighlightKind kind, const RenderTheme &theme)
{
  switch(kind)
  {
  case HighlightKind::Comment:
    return theme.comment;
  case HighlightKind::Keyword:
    return theme.keyword;
  case HighlightKind::String:
    return theme.string_;
  case HighlightKind::Function:
    return theme.function;
  case HighlightKind::Variable:
    return theme.variable;
  case HighlightKind::Type:
    return theme.type;
  case HighlightKind::Number:
    return theme.number;
  case HighlightKind::Operator:
    return theme.op;
  case HighlightKind::Property:
    return theme.property;
  case HighlightKind::Constant:
    return theme.constant;
  case HighlightKind::Punctuation:
    return theme.punctuation;
  case HighlightKind::Plain:
  default:
    return theme.plain;
  }
}

std::vector<std::pair<size_t, size_t>> split_lines(std::string_view source)
{
  std::vector<std::pair<size_t, size_t>> lines;
  size_t start = 0;
  while(start <= source.size())
  {
    const size_t end = source.find('\n', start);
    if(end == std::string_view::npos)
    {
      lines.push_back({start, source.size()});
      break;
    }
    lines.push_back({start, end});
    start = end + 1;
    if(start == source.size()) lines.push_back({start, start});
  }
  if(lines.empty()) lines.push_back({0, 0});
  return lines;
}

void render_styled_range(std::string_view source,
                         std::string_view line,
                         size_t line_start,
                         const std::vector<HighlightSpan> &spans,
                         const RenderTheme &theme)
{
  if(line.empty())
  {
    ImGui::TextUnformatted(" ");
    return;
  }

  bool first = true;
  size_t pos = line_start;
  for(const HighlightSpan &span : spans)
  {
    if(span.end <= line_start) continue;
    if(span.start >= line_start + line.size()) break;

    const size_t span_start = std::max(span.start, line_start);
    const size_t span_end = std::min(span.end, line_start + line.size());
    if(span_start > pos)
    {
      if(!first) ImGui::SameLine(0.0f, 0.0f);
      ImGui::TextUnformatted(source.data() + pos, source.data() + span_start);
      first = false;
    }
    if(span_end > span_start)
    {
      if(!first) ImGui::SameLine(0.0f, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, color_for_kind(span.kind, theme));
      ImGui::TextUnformatted(source.data() + span_start, source.data() + span_end);
      ImGui::PopStyleColor();
      first = false;
      pos = span_end;
    }
  }

  if(pos < line_start + line.size())
  {
    if(!first) ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(source.data() + pos, source.data() + line_start + line.size());
  }
}
} // namespace

namespace MarkdownCodeHighlight
{
void register_language(LanguageDefinition definition)
{
  if(definition.name.empty() || definition.language_factory == nullptr) return;

  Registry &r = registry();
  std::scoped_lock lock(r.mutex);

  const std::string normalized_name = normalize_alias(definition.name);
  if(normalized_name.empty()) return;

  definition.name = normalized_name;
  for(std::string &alias : definition.aliases)
    alias = normalize_alias(alias);

  for(LanguageDefinition &existing : r.languages)
  {
    if(existing.name == normalized_name)
    {
      existing = std::move(definition);
      return;
    }
  }

  r.languages.push_back(std::move(definition));
}

const LanguageDefinition *find_language(std::string_view fence_info)
{
  register_builtin_languages();

  const std::string requested = fence_language_token(fence_info);
  if(requested.empty()) return nullptr;

  Registry &r = registry();
  std::scoped_lock lock(r.mutex);
  for(const LanguageDefinition &language : r.languages)
  {
    if(language.name == requested) return &language;
    for(const std::string &alias : language.aliases)
    {
      if(alias == requested) return &language;
    }
  }
  return nullptr;
}

HighlightedCodeBlock highlight_code_block(std::string_view fence_info, std::string_view source)
{
  register_builtin_languages();

  HighlightedCodeBlock out;
  out.requested_language = fence_language_token(fence_info);

  const LanguageDefinition *language = find_language(fence_info);
  if(language == nullptr || language->language_factory == nullptr) return out;

  out.recognized_language = true;
  out.resolved_language = language->name;

  TSParser *parser = ts_parser_new();
  if(parser == nullptr) return out;

  const TSLanguage *ts_language = language->language_factory();
  if(ts_language == nullptr || !ts_parser_set_language(parser, ts_language))
  {
    ts_parser_delete(parser);
    return out;
  }

  TSTree *tree = ts_parser_parse_string(parser, nullptr, source.data(), static_cast<uint32_t>(source.size()));
  ts_parser_delete(parser);
  if(tree == nullptr) return out;

  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  TSQuery *query = ts_query_new(
      ts_language,
      language->highlight_query.data(),
      static_cast<uint32_t>(language->highlight_query.size()),
      &error_offset,
      &error_type);
  if(query == nullptr)
  {
    (void)error_offset;
    (void)error_type;
    ts_tree_delete(tree);
    return out;
  }

  std::vector<HighlightKind> byte_kinds(source.size(), HighlightKind::Plain);
  std::vector<int> byte_priorities(source.size(), 0);

  TSQueryCursor *cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

  TSQueryMatch match;
  uint32_t capture_index = 0;
  while(ts_query_cursor_next_capture(cursor, &match, &capture_index))
  {
    const TSQueryCapture capture = match.captures[capture_index];
    uint32_t capture_name_len = 0;
    const char *capture_name = ts_query_capture_name_for_id(query, capture.index, &capture_name_len);
    if(capture_name == nullptr || capture_name_len == 0) continue;

    const HighlightKind kind = classify_capture_name(std::string_view(capture_name, capture_name_len));
    if(kind == HighlightKind::Plain) continue;

    const uint32_t start = ts_node_start_byte(capture.node);
    const uint32_t end = ts_node_end_byte(capture.node);
    if(start >= end || start >= source.size()) continue;

    const size_t bounded_end = std::min<size_t>(end, source.size());
    const int priority = kind_priority(kind);
    for(size_t i = start; i < bounded_end; ++i)
    {
      if(priority >= byte_priorities[i])
      {
        byte_priorities[i] = priority;
        byte_kinds[i] = kind;
      }
    }
  }

  ts_query_cursor_delete(cursor);
  ts_query_delete(query);
  ts_tree_delete(tree);

  size_t span_start = 0;
  while(span_start < byte_kinds.size())
  {
    const HighlightKind kind = byte_kinds[span_start];
    size_t span_end = span_start + 1;
    while(span_end < byte_kinds.size() && byte_kinds[span_end] == kind) ++span_end;
    out.spans.push_back({span_start, span_end, kind});
    span_start = span_end;
  }

  return out;
}

void render_code_block(std::string_view fence_info, std::string_view source, int imgui_id)
{
  const HighlightedCodeBlock highlighted = highlight_code_block(fence_info, source);
  const RenderTheme theme;
  const std::vector<std::pair<size_t, size_t>> lines = split_lines(source);

  const std::string label = highlighted.recognized_language
                                ? highlighted.resolved_language
                                : (highlighted.requested_language.empty() ? std::string("text") : highlighted.requested_language);

  const int digits = std::max(2, static_cast<int>(std::floor(std::log10(std::max<size_t>(1, lines.size()))) + 1.0));
  const std::string gutter_sample(static_cast<size_t>(digits), '0');
  const float gutter_width = ImGui::CalcTextSize(gutter_sample.c_str()).x + 18.0f;
  const float line_height = ImGui::GetTextLineHeightWithSpacing();
  const float body_height = std::min(420.0f, 18.0f + line_height * static_cast<float>(std::max<size_t>(1, lines.size())));
  const float frame_height = body_height + 26.0f;

  ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.background);
  ImGui::PushStyleColor(ImGuiCol_Border, theme.border);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

  if(ImGui::BeginChildFrame(static_cast<ImGuiID>(imgui_id), ImVec2(0.0f, frame_height), ImGuiWindowFlags_HorizontalScrollbar))
  {
    ImGui::PushStyleColor(ImGuiCol_Text, theme.label);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    for(size_t line_index = 0; line_index < lines.size(); ++line_index)
    {
      const auto [line_start, line_end] = lines[line_index];
      const std::string_view line(source.data() + line_start, line_end - line_start);

      ImGui::PushStyleColor(ImGuiCol_Text, theme.gutter);
      ImGui::Text("%*zu", digits, line_index + 1);
      ImGui::PopStyleColor();
      ImGui::SameLine(gutter_width);
      render_styled_range(source, line, line_start, highlighted.spans, theme);
    }
  }

  ImGui::EndChildFrame();
  if(ImGui::BeginPopupContextItem("##code_block_popup", ImGuiPopupFlags_MouseButtonRight))
  {
    if(ImGui::MenuItem("Copy code"))
    {
      ImGui::SetClipboardText(std::string(source).c_str());
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}
} // namespace MarkdownCodeHighlight
